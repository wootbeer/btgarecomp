// Sound packs: replacement sound effects, installed like mods.
//
// A pack is a .zip in the mods folder (or a folder with a mod.json, like any
// mod) holding btga_audio.json, which marks it as a sound pack, and the
// effects it replaces as sfx/<name>.wav, .ogg or .mp3. The names are
// N64SoundListTool's: BANK_00_INSTR_0000 to BANK_00_INSTR_004B, each the
// effect's index in the game's sound table (ROM 0x58FAE0, 76 effects). Any
// effect without a file keeps the game's (or the BattleTanx 1 one, when that
// option is on). Packs show in the Mods tab and apply when the game starts;
// where two replace the same effect, the one higher in the list wins.
//
// Speed: N64SoundListTool writes every one of this game's effects as a
// 22,050 Hz WAV (its default for the bank; the game's own rates differ by
// effect). So a file plays relative to that: at 22,050 Hz, at the original's
// speed; at 44,100 Hz, twice as fast for the same samples. A rip edited and
// saved at its rate plays like the original, and a new sound plays in game as
// the rips do next to it in a media player. Each file is resampled to that
// speed, so it never plays faster than the original did (the mixer can't go
// past 2.0x, round 175), and the effect's base note is the original's.
//
// btga_audio.json, all optional:
//   {
//     "name": "...", "author": "...",
//     "sfx": {
//       "BANK_00_INSTR_000F": { "semitones": -2, "volume": 0.8, "loop_start": 1200, "loop_end": 48000 }
//     }
//   }
// semitones shifts the pitch and speed (fractions allowed), volume scales it,
// loop_start and loop_end are in the file's samples. Loops: only effects that
// loop in the game can loop (the table has no room for a new loop). Their
// loop is the manifest's, else the WAV's (smpl chunk) or the Ogg's
// (LOOPSTART/LOOPEND or LOOPLENGTH tags), else the whole file.
//
// Each sound is converted to the game's sample format: VADPCM with a codebook
// fitted to it (vadpcm.cpp), added past the end of the ROM, and the effect's
// wave table pointed at it, as the BattleTanx 1 option does (bt1_sounds.cpp).
#include "btga_config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"
#include "vadpcm.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

namespace {
    // This game's sound table (libmus "N64 PtrTablesV2") and its sample data.
    constexpr uint32_t ga_ptr_bank = 0x58FAE0;
    constexpr uint32_t ga_wave_bank = 0x593A28;
    constexpr uint32_t ga_samples = 76;
    // The rate N64SoundListTool writes these effects at: a file at this rate
    // plays at the original's speed.
    constexpr double rip_rate = 22050.0;
    const std::string pack_file = "btga_audio.json";

    uint32_t read_be32(const uint8_t* p) {
        return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }

    void write_be32(uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v >> 24);
        p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);
        p[3] = (uint8_t)v;
    }

    // A decoded file: mono, -1 to 1, at its rate, with its loop if it has one.
    struct Audio {
        std::vector<float> samples;
        double rate = 0.0;
        int64_t loop_start = -1;
        int64_t loop_end = -1;
    };

    uint32_t le16(const uint8_t* p) {
        return (uint32_t)p[0] | (uint32_t)p[1] << 8;
    }

    uint32_t le32(const uint8_t* p) {
        return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }

    // PCM (8, 16, 24 or 32-bit) or 32-bit float, any channel count; the smpl chunk's first loop.
    bool read_wav(const std::vector<char>& file, Audio& out, std::string& error) {
        const uint8_t* d = reinterpret_cast<const uint8_t*>(file.data());
        const size_t size = file.size();
        if ((size < 12) || (std::memcmp(d, "RIFF", 4) != 0) || (std::memcmp(d + 8, "WAVE", 4) != 0)) {
            error = "not a WAV file";
            return false;
        }
        uint32_t format = 0, channels = 0, rate = 0, bits = 0;
        const uint8_t* data = nullptr;
        size_t data_size = 0;
        for (size_t pos = 12; pos + 8 <= size;) {
            const uint32_t chunk_size = le32(d + pos + 4);
            const uint8_t* chunk = d + pos + 8;
            const size_t available = std::min<size_t>(chunk_size, size - pos - 8);
            if ((std::memcmp(d + pos, "fmt ", 4) == 0) && (available >= 16)) {
                format = le16(chunk);
                channels = le16(chunk + 2);
                rate = le32(chunk + 4);
                bits = le16(chunk + 14);
                if ((format == 0xFFFE) && (available >= 26)) {
                    format = le16(chunk + 24); // WAVE_FORMAT_EXTENSIBLE: the subformat
                }
            }
            else if (std::memcmp(d + pos, "data", 4) == 0) {
                data = chunk;
                data_size = available;
            }
            else if ((std::memcmp(d + pos, "smpl", 4) == 0) && (available >= 36 + 24) && (le32(chunk + 28) > 0)) {
                out.loop_start = le32(chunk + 36 + 8);
                out.loop_end = (int64_t)le32(chunk + 36 + 12) + 1; // inclusive in the file
            }
            pos += 8 + chunk_size + (chunk_size & 1);
        }
        const uint32_t bytes = bits / 8;
        if ((data == nullptr) || (channels == 0) || (rate == 0) || (bytes == 0) ||
            !(((format == 1) && (bytes >= 1) && (bytes <= 4)) || ((format == 3) && (bytes == 4)))) {
            error = "unsupported WAV format (use 8, 16, 24 or 32-bit PCM, or 32-bit float)";
            return false;
        }
        const size_t frames = data_size / (bytes * channels);
        out.rate = rate;
        out.samples.resize(frames);
        for (size_t f = 0; f < frames; f++) {
            double sum = 0.0;
            for (uint32_t c = 0; c < channels; c++) {
                const uint8_t* s = data + (f * channels + c) * bytes;
                double v;
                if (format == 3) {
                    float x;
                    std::memcpy(&x, s, 4);
                    v = x;
                }
                else if (bytes == 1) {
                    v = (s[0] - 128) / 128.0;
                }
                else {
                    int32_t x = 0;
                    for (uint32_t b = 0; b < bytes; b++) {
                        x |= (int32_t)s[b] << (8 * (4 - bytes + b));
                    }
                    v = x / 2147483648.0;
                }
                sum += v;
            }
            out.samples[f] = (float)(sum / channels);
        }
        return true;
    }

    bool read_ogg(const std::vector<char>& file, Audio& out, std::string& error) {
        int vorbis_error = 0;
        stb_vorbis* v = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(file.data()), (int)file.size(), &vorbis_error, nullptr);
        if (v == nullptr) {
            error = "not an Ogg Vorbis file";
            return false;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(v);
        out.rate = info.sample_rate;
        // Loop tags, as many games' Ogg files carry them.
        const stb_vorbis_comment comments = stb_vorbis_get_comment(v);
        int64_t loop_length = -1;
        for (int i = 0; i < comments.comment_list_length; i++) {
            std::string c = comments.comment_list[i];
            const size_t eq = c.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string key = c.substr(0, eq);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return (char)std::toupper(ch); });
            const int64_t value = std::strtoll(c.c_str() + eq + 1, nullptr, 10);
            if (key == "LOOPSTART") {
                out.loop_start = value;
            }
            else if (key == "LOOPEND") {
                out.loop_end = value;
            }
            else if (key == "LOOPLENGTH") {
                loop_length = value;
            }
        }
        if ((out.loop_start >= 0) && (out.loop_end < 0) && (loop_length > 0)) {
            out.loop_end = out.loop_start + loop_length;
        }
        const int channels = std::max(info.channels, 1);
        std::vector<float> buffer(4096 * channels);
        for (;;) {
            const int frames = stb_vorbis_get_samples_float_interleaved(v, channels, buffer.data(), (int)buffer.size());
            if (frames <= 0) {
                break;
            }
            for (int f = 0; f < frames; f++) {
                float sum = 0.0f;
                for (int c = 0; c < channels; c++) {
                    sum += buffer[f * channels + c];
                }
                out.samples.push_back(sum / channels);
            }
        }
        stb_vorbis_close(v);
        return true;
    }

    bool read_mp3(const std::vector<char>& file, Audio& out, std::string& error) {
        drmp3_config config{};
        drmp3_uint64 frames = 0;
        float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(file.data(), file.size(), &config, &frames, nullptr);
        if ((pcm == nullptr) || (config.channels == 0)) {
            error = "not an MP3 file";
            return false;
        }
        out.rate = config.sampleRate;
        out.samples.resize((size_t)frames);
        for (size_t f = 0; f < (size_t)frames; f++) {
            float sum = 0.0f;
            for (uint32_t c = 0; c < config.channels; c++) {
                sum += pcm[f * config.channels + c];
            }
            out.samples[f] = sum / config.channels;
        }
        drmp3_free(pcm, nullptr);
        return true;
    }

    // Windowed sinc (Blackman, 16 zero crossings each side), low-passed below
    // the lower of the two rates.
    std::vector<float> resample(const std::vector<float>& in, double ratio) {
        if (std::fabs(ratio - 1.0) < 1e-9) {
            return in;
        }
        const size_t out_size = (size_t)std::llround(in.size() * ratio);
        std::vector<float> out(out_size);
        const double cutoff = std::min(1.0, ratio) * 0.97;
        constexpr int zero_crossings = 16;
        const double half_width = zero_crossings / cutoff;
        constexpr double pi = 3.14159265358979323846;
        for (size_t n = 0; n < out_size; n++) {
            const double center = n / ratio;
            const int64_t first = (int64_t)std::ceil(center - half_width);
            const int64_t last = (int64_t)std::floor(center + half_width);
            double sum = 0.0, weight = 0.0;
            for (int64_t k = first; k <= last; k++) {
                const double x = (k - center) * cutoff;
                const double sinc = (std::fabs(x) < 1e-9) ? 1.0 : std::sin(pi * x) / (pi * x);
                const double w = (k - center + half_width) / (2.0 * half_width);
                const double window = 0.42 - 0.5 * std::cos(2.0 * pi * w) + 0.08 * std::cos(4.0 * pi * w);
                const double tap = sinc * window;
                weight += tap;
                if ((k >= 0) && (k < (int64_t)in.size())) {
                    sum += tap * in[(size_t)k];
                }
            }
            out[n] = (float)((weight != 0.0) ? sum / weight : 0.0);
        }
        return out;
    }

    // Per effect, the mod order index of the pack that replaced it (lower is
    // higher in the list), and whether its BattleTanx 1 shift was undone.
    std::array<size_t, ga_samples> replaced_by = [] {
        std::array<size_t, ga_samples> a;
        a.fill(std::numeric_limits<size_t>::max());
        return a;
    }();
    std::array<bool, ga_samples> bt1_shift_undone{};

    std::string effect_name(uint32_t effect) {
        char name[32];
        std::snprintf(name, sizeof(name), "BANK_00_INSTR_%04X", effect);
        return name;
    }

    // Reads sfx/<name>.<ext> from the pack, trying each format.
    bool read_effect_file(const recomp::mods::ModFileHandle& files, const std::string& name, Audio& audio, std::string& path, std::string& error) {
        static const struct {
            const char* extension;
            bool (*reader)(const std::vector<char>&, Audio&, std::string&);
        } formats[] = {
            { ".wav", read_wav }, { ".WAV", read_wav }, { ".ogg", read_ogg }, { ".OGG", read_ogg },
            { ".mp3", read_mp3 }, { ".MP3", read_mp3 },
        };
        for (const auto& format : formats) {
            path = "sfx/" + name + format.extension;
            if (!files.file_exists(path)) {
                continue;
            }
            bool exists = false;
            const std::vector<char> data = files.read_file(path, exists);
            if (!exists) {
                error = "can't be read";
                return false;
            }
            return format.reader(data, audio, error);
        }
        path.clear();
        return false;
    }

    void on_pack_enabled(recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
        const std::string& mod_id = mod.manifest.mod_id;
        const recomp::mods::ModFileHandle* files = mod.manifest.file_handle.get();
        if (files == nullptr) {
            return;
        }
        const size_t order = context.get_mod_order_index(mod_id);

        nlohmann::json manifest = nlohmann::json::object();
        {
            bool exists = false;
            const std::vector<char> text = files->read_file(pack_file, exists);
            if (exists) {
                manifest = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
                if (manifest.is_discarded() || !manifest.is_object()) {
                    std::fprintf(stderr, "Sound pack %s: %s isn't valid JSON; using no overrides.\n", mod_id.c_str(), pack_file.c_str());
                    manifest = nlohmann::json::object();
                }
            }
        }
        const nlohmann::json no_overrides = nlohmann::json::object();
        const nlohmann::json& sfx = (manifest.contains("sfx") && manifest["sfx"].is_object()) ? manifest["sfx"] : no_overrides;

        std::span<const uint8_t> loaded = recomp::get_rom();
        if ((loaded.size() < ga_wave_bank) || (std::memcmp(loaded.data() + ga_ptr_bank, "N64 PtrTablesV2", 15) != 0)) {
            return;
        }
        std::vector<uint8_t> rom(loaded.begin(), loaded.end());
        uint8_t* bank = rom.data() + ga_ptr_bank;
        if (read_be32(bank + 0x20) != ga_samples) {
            return;
        }
        const uint32_t basenotes = read_be32(bank + 0x24);
        const uint32_t waves = read_be32(bank + 0x2C);

        uint32_t replaced = 0;
        for (uint32_t effect = 0; effect < ga_samples; effect++) {
            if (replaced_by[effect] <= order) {
                continue; // a pack higher in the list has it
            }
            const std::string name = effect_name(effect);
            Audio audio;
            std::string path, error;
            if (!read_effect_file(*files, name, audio, path, error)) {
                if (!path.empty()) {
                    std::fprintf(stderr, "Sound pack %s: %s: %s\n", mod_id.c_str(), path.c_str(), error.c_str());
                }
                continue;
            }
            if (audio.samples.empty() || (audio.rate <= 0.0)) {
                std::fprintf(stderr, "Sound pack %s: %s is empty.\n", mod_id.c_str(), path.c_str());
                continue;
            }

            double semitones = 0.0, volume = 1.0;
            if (sfx.contains(name) && sfx[name].is_object()) {
                const nlohmann::json& o = sfx[name];
                if (o.contains("semitones") && o["semitones"].is_number()) {
                    semitones = o["semitones"].get<double>();
                }
                if (o.contains("volume") && o["volume"].is_number()) {
                    volume = std::max(0.0, o["volume"].get<double>());
                }
                if (o.contains("loop_start") && o["loop_start"].is_number_integer()) {
                    audio.loop_start = o["loop_start"].get<int64_t>();
                }
                if (o.contains("loop_end") && o["loop_end"].is_number_integer()) {
                    audio.loop_end = o["loop_end"].get<int64_t>();
                }
            }

            // To the rip's speed: rip_rate samples a second of the file, shifted by semitones.
            const double target_rate = rip_rate / std::pow(2.0, semitones / 12.0);
            const double ratio = target_rate / audio.rate;
            const std::vector<float> resampled = resample(audio.samples, ratio);
            std::vector<int16_t> pcm(resampled.size());
            for (size_t i = 0; i < pcm.size(); i++) {
                pcm[i] = (int16_t)std::clamp<long>(std::lround(resampled[i] * volume * 32767.0), -32768, 32767);
            }
            if (pcm.size() < (size_t)btga::vadpcm::kFrameSamples) {
                pcm.resize(btga::vadpcm::kFrameSamples, 0);
            }

            uint8_t* wave = bank + read_be32(bank + waves + effect * 4);
            const uint32_t loop = read_be32(wave + 12);
            uint8_t* book_data = bank + read_be32(wave + 16);
            if ((read_be32(book_data) != (uint32_t)btga::vadpcm::kOrder) || (read_be32(book_data + 4) != (uint32_t)btga::vadpcm::kPredictors)) {
                continue;
            }

            // The loop, in whole frames: the game's loop state is the frame that holds its start.
            uint32_t loop_start = 0, loop_end = (uint32_t)pcm.size();
            if (loop != 0) {
                if ((audio.loop_start >= 0) && (audio.loop_end > audio.loop_start)) {
                    loop_start = (uint32_t)std::llround(audio.loop_start * ratio);
                    loop_end = (uint32_t)std::llround(std::min<int64_t>(audio.loop_end, (int64_t)audio.samples.size()) * ratio);
                }
                const uint32_t frame = btga::vadpcm::kFrameSamples;
                loop_start = std::min<uint32_t>((loop_start + frame / 2) / frame * frame, (uint32_t)pcm.size());
                uint32_t length = (std::max(loop_end, loop_start) - loop_start + frame / 2) / frame * frame;
                if (length < 2 * frame) {
                    loop_start = 0;
                    length = (uint32_t)pcm.size() / frame * frame;
                }
                loop_end = loop_start + std::max<uint32_t>(length, frame);
                if (pcm.size() < loop_end) {
                    pcm.resize(loop_end, 0);
                }
            }

            const btga::vadpcm::Book book = btga::vadpcm::design_book(pcm);
            const std::vector<uint8_t> data = btga::vadpcm::encode(book, pcm);

            const size_t offset = (rom.size() + 15) & ~size_t(15);
            if (offset + data.size() > 0x0FC00000) { // the cartridge address space
                std::fprintf(stderr, "Sound pack %s: no room for %s.\n", mod_id.c_str(), path.c_str());
                continue;
            }
            rom.resize(offset);
            rom.insert(rom.end(), data.begin(), data.end());
            bank = rom.data() + ga_ptr_bank; // the ROM may have moved
            wave = bank + read_be32(bank + waves + effect * 4);
            book_data = bank + read_be32(wave + 16);

            write_be32(wave, (uint32_t)(offset - ga_wave_bank));
            write_be32(wave + 4, (uint32_t)data.size());
            btga::vadpcm::write_book_coefficients(book, book_data + 8);
            if (loop != 0) {
                uint8_t* l = bank + loop;
                write_be32(l, loop_start);
                write_be32(l + 4, loop_end);
                // l + 8: the loop count stays the game's.
                const std::vector<int16_t> decoded = btga::vadpcm::decode(book, data);
                for (int i = 0; i < btga::vadpcm::kFrameSamples; i++) {
                    const uint16_t v = (uint16_t)decoded[loop_start + i];
                    l[12 + i * 2] = (uint8_t)(v >> 8);
                    l[12 + i * 2 + 1] = (uint8_t)v;
                }
            }
            // The original's base note: the speed is in the samples now.
            if (!bt1_shift_undone[effect]) {
                bank[basenotes + effect] = (uint8_t)((int8_t)bank[basenotes + effect] - btga::bt1_sounds_semitones(effect));
                bt1_shift_undone[effect] = true;
            }
            replaced_by[effect] = order;
            replaced++;
        }

        if (replaced > 0) {
            std::printf("Sound pack %s: %u effects replaced.\n", mod_id.c_str(), replaced);
            recomp::set_rom_contents(std::move(rom));
        }
    }
}

void btga::register_sound_packs() {
    const recomp::mods::ModContentTypeId type = recomp::mods::register_mod_content_type({
        .content_filename = pack_file,
        .allow_runtime_toggle = false,
        .on_enabled = on_pack_enabled,
        .on_disabled = nullptr,
        .on_reordered = nullptr,
    });
    // A .zip with btga_audio.json needs no mod.json: it's named after the file.
    recomp::mods::register_mod_container_type("zip", { type }, false);
}
