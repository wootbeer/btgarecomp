// BattleTanx 1 Sound Effects on the Sound tab (round 159, issue #3).
//
// Many of this game's sound effects are the first game's, made smaller: the
// first game keeps them at 44.1 kHz, this one has them resampled 23
// semitones down (a factor of 3.775), so they lost everything above about
// 5.8 kHz. This plays the first game's versions instead, for the effects the
// owner matched by ear (kReplacements).
//
// Load BattleTanx ROM keeps the first game's effects bank -- its ALBankFile
// at ROM 0x215620 and its sample data at 0x100000 -- in the app's folder
// (no ROM data ships with the port). When the game starts with the option
// on, that sample data goes past the end of the loaded ROM, and this game's
// effects table is edited in the ROM before anything reads it:
//
// - The table is libmus's ptr_bank_t ("N64 PtrTablesV2") at ROM 0x58FAE0,
//   DMA'd as is at boot: per sample a base note (+0x24, an offset to bytes),
//   a fine tune (+0x28, floats) and a wave table (+0x2C, offsets to
//   ALWaveTables: base, len, type/flags, loop, book, all offsets; sample
//   bases from the wave bank at ROM 0x593A28).
// - A replaced sample's wave table gets the first game's base, length,
//   predictor book (both games use order 2 with 4 predictors, so it fits in
//   place) and, for those that loop, its loop with its state.
// - Its base note goes up by as many semitones as this game slowed the sample
//   (23, 18 or 12, kReplacements): despite the name, libmus plays a sample
//   faster the higher that value is (measured: -23 made a replaced effect's
//   playback ratio 3.7 times lower). So the extra samples play at the same
//   pitch and speed. The mixer can't go past a ratio of 2.0 at 22,050 Hz
//   output; the effects play at 0.236-0.53 (measured), so 23 semitones up
//   stays under it for those (ga22 aside, below).
//   The detune floats (+0x28) aren't used: libmus adds them to the note, and
//   setting one to 1.0, 4.0 or 12.0 all raised the playback ratio by the same
//   3.8% (round 168). The shifts are whole semitones anyway (round 181).
// - Where this game's sample starts later in the sound, the first game's
//   starts that many whole ADPCM frames in, faded in over 2.9 ms (fade_in;
//   none of those loop).
//
// Nothing in the game's code changes.
#include "btga_netplay.h"
#include "btga_config.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "librecomp/game.hpp"
#include "recompui/config.h"
#include "recompui/recompui.h"
#include "util/file.h"
#include "vadpcm.h"

namespace {
    const std::string option_id = "bt1_sound_effects";
    bool option_added = false;

    // In BattleTanx (USA): the effects bank and its sample data.
    constexpr uint32_t bt1_ctl = 0x215620;
    constexpr uint32_t bt1_ctl_size = 0x2890;
    constexpr uint32_t bt1_tbl = 0x100000;
    constexpr uint32_t bt1_tbl_size = 0x11561A;
    constexpr uint64_t bt1_ctl_fingerprint = 0xa8727da37fe81e09ull; // every byte
    constexpr uint64_t bt1_tbl_fingerprint = 0x6c139a27c2e7c28eull; // 16 bytes every 4 KB

    // In this game's ROM.
    constexpr uint32_t ga_ptr_bank = 0x58FAE0;
    constexpr uint32_t ga_wave_bank = 0x593A28;
    constexpr uint32_t ga_samples = 76;
    // This game's sample -> the first game's sound (instrument 0 of its only
    // bank), and how far this game had slowed it down: the sample's base note
    // goes up that many semitones, so the first game's 44.1 kHz sample plays
    // at the same pitch and speed.
    //
    // The shift is exact (round 181): the first game's sample, resampled, lines
    // up with this game's point by point (0.58-0.995, wrong pairs ~0.15) at
    // exactly 12, 18 or 23 semitones (2^(n/12) = 2, 2.83, 3.775), and nowhere
    // else from 1.3x to 6.5x. The samples' length ratios (round 168) were off
    // where one game had trimmed the sound. The owner matched every pair by ear.
    // Names in the comments are N64SoundListTool's: GA = BANK_00_INSTR_<ga, hex>,
    // BT1 = BANK_00_INSTR_0000_SND_<bt1, hex>.
    //
    // Not ga22 (was the first game's sound 26, 23 semitones, round 159): the
    // grenade's bounce plays it at a ratio of 1.414, which the first game's
    // sample would need at 5.3, past the mixer's 2.0. It came out 17 semitones
    // too low and 2.7 times too long (round 175).
    struct Replacement {
        uint8_t ga;
        uint8_t bt1;
        int8_t semitones;
        // ADPCM frames (16 samples, 9 bytes) of the first game's sample to
        // skip, where this game's starts later in the sound.
        uint16_t skip_frames = 0;
    };
    constexpr Replacement kReplacements[] = {
        { 0, 39, 23 }, { 2, 0, 23 }, { 6, 31, 23 }, { 8, 3, 23 }, { 10, 5, 23 }, { 11, 6, 23 },
        { 12, 22, 23 }, { 13, 21, 23 }, { 16, 7, 23 }, { 18, 9, 23 }, { 19, 25, 23 }, { 21, 11, 23 },
        { 26, 28, 23 }, { 28, 30, 23 }, { 30, 20, 23 }, { 32, 34, 23 }, { 33, 13, 23 }, { 34, 14, 23 },
        { 35, 35, 23 }, { 39, 18, 23 }, { 59, 42, 23 },
        { 3, 1, 18 },   // the first game's is shorter: this game's has a longer tail
        { 4, 2, 18 }, { 7, 32, 18 }, { 9, 4, 18 }, { 15, 24, 18 }, { 17, 8, 18 }, { 20, 10, 18 },
        { 25, 27, 18 }, { 27, 29, 18 }, { 31, 12, 18 }, { 36, 15, 18 },
        { 40, 37, 18, 292 }, // this game's starts 4,672 samples (0.1 s) into the first game's
        { 14, 23, 12 },
    };

    // What apply_bt1_sounds() added to each effect's base note.
    int8_t applied_semitones[ga_samples] = {};

    // The saved file: magic, then the bank and the sample data.
    constexpr char magic[8] = { 'B', 'T', 'X', 'S', 'F', 'X', '1', 0 };

    std::filesystem::path saved_path() {
        return recompui::file::get_app_folder_path() / "battletanx1_sounds.bin";
    }

    uint32_t read_be32(const uint8_t* p) {
        return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }

    uint16_t read_be16(const uint8_t* p) {
        return (uint16_t)(p[0] << 8 | p[1]);
    }

    void write_be32(uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v >> 24);
        p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);
        p[3] = (uint8_t)v;
    }

    // To big-endian (.z64) order; false if it isn't an N64 ROM.
    bool normalize(std::vector<uint8_t>& rom) {
        if (rom.size() < 0x40) {
            return false;
        }
        uint32_t first = read_be32(rom.data());
        if (first == 0x80371240) {
            return true;
        }
        if (first == 0x37804012) { // .v64: 16-bit words swapped
            for (size_t i = 0; i + 1 < rom.size(); i += 2) {
                std::swap(rom[i], rom[i + 1]);
            }
            return true;
        }
        if (first == 0x40123780) { // .n64: little-endian words
            for (size_t i = 0; i + 3 < rom.size(); i += 4) {
                std::swap(rom[i], rom[i + 3]);
                std::swap(rom[i + 1], rom[i + 2]);
            }
            return true;
        }
        return false;
    }

    uint64_t fingerprint(const uint8_t* data, uint32_t size, uint32_t step) {
        uint64_t h = 0xcbf29ce484222325ull;
        for (uint32_t i = 0; i < size; i += step) {
            for (uint32_t j = i; j < i + 16 && j < size; j++) {
                h = (h ^ data[j]) * 0x100000001b3ull;
            }
        }
        return h;
    }

    // In the game's own UI: a system message box can open behind the window.
    void show_message(const std::string& text) {
        recompui::open_info_prompt("BattleTanx 1 Sound Effects", text, "OK", [] {}, recompui::ButtonStyle::Primary);
    }

    // Load BattleTanx ROM: keeps the first game's effects bank and samples.
    void load_bt1() {
        recompui::file::open_file_dialog([](bool success, const std::filesystem::path& path) {
            if (!success) {
                return;
            }
            std::vector<uint8_t> rom;
            {
                std::ifstream file(path, std::ios::binary);
                if (!file.good()) {
                    show_message("Failed to open the ROM file.");
                    return;
                }
                rom.assign(std::istreambuf_iterator<char>(file), {});
            }
            if (!normalize(rom) || rom.size() < bt1_ctl + bt1_ctl_size ||
                fingerprint(rom.data() + bt1_ctl, bt1_ctl_size, 16) != bt1_ctl_fingerprint ||
                fingerprint(rom.data() + bt1_tbl, bt1_tbl_size, 0x1000) != bt1_tbl_fingerprint) {
                show_message("This isn't BattleTanx (USA). Choose the first BattleTanx game's ROM, not Global Assault.");
                return;
            }
            std::vector<uint8_t> out(magic, magic + sizeof(magic));
            out.insert(out.end(), rom.begin() + bt1_ctl, rom.begin() + bt1_ctl + bt1_ctl_size);
            out.insert(out.end(), rom.begin() + bt1_tbl, rom.begin() + bt1_tbl + bt1_tbl_size);
            std::error_code ec;
            std::filesystem::create_directories(saved_path().parent_path(), ec);
            std::ofstream file(saved_path(), std::ios::binary);
            file.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
            if (!file.good()) {
                show_message("Failed to save the BattleTanx sound effects.");
                return;
            }
            show_message("The BattleTanx sound effects are ready. Turn on BattleTanx 1 Sound Effects to use them.");
        });
    }

    bool enabled() {
        return option_added &&
            std::get<bool>(recompui::config::get_sound_config().get_option_value(option_id));
    }

    // One sound of the first game's bank: its wave table, from the saved bank.
    struct Bt1Wave {
        uint32_t base, len;
        const uint8_t* book; // order, npredictors, coefficients
        const uint8_t* loop; // start, end, count, state; nullptr if it doesn't loop
    };

    // A sample that starts skip_frames into the first game's: those frames
    // are decoded with their history, faded in over fade_frames (raised
    // cosine) and encoded again from silence. Cut in as is, it would start
    // with a step from silence, and its first frame decoded without its
    // history would be off (by 1,197 on ga40's first sample). Past the fade
    // the decoder's history matches and the rest plays as before.
    constexpr uint32_t fade_frames = 8; // 128 samples, 2.9 ms at 44.1 kHz
    void fade_in(uint8_t* sample, uint32_t skip_frames, const uint8_t* book_data) {
        const btga::vadpcm::Book b = btga::vadpcm::read_book(book_data);
        int32_t hist[2] = { 0, 0 }, pcm[16];
        for (uint32_t f = 0; f < skip_frames; f++) {
            btga::vadpcm::decode_frame(b, sample + f * 9, hist, pcm);
        }
        int32_t faded[fade_frames][16];
        for (uint32_t f = 0; f < fade_frames; f++) {
            btga::vadpcm::decode_frame(b, sample + (skip_frames + f) * 9, hist, pcm);
            for (int i = 0; i < 16; i++) {
                const double x = std::min(1.0, double(f * 16 + i + 1) / double(fade_frames * 16));
                faded[f][i] = (int32_t)std::lround(pcm[i] * (0.5 - 0.5 * std::cos(3.14159265358979 * x)));
            }
        }
        int32_t enc_hist[2] = { 0, 0 };
        for (uint32_t f = 0; f < fade_frames; f++) {
            btga::vadpcm::encode_frame(b, faded[f], enc_hist, sample + (skip_frames + f) * 9);
        }
    }

    // Sound `index` of instrument 0 of bank 0 (ALBankFile layout), or false.
    bool find_bt1_wave(const std::vector<uint8_t>& ctl, uint32_t index, Bt1Wave& out) {
        auto in = [&](uint32_t offset, uint32_t size) { return offset < ctl.size() && size <= ctl.size() - offset; };
        if (!in(0, 8) || read_be16(ctl.data() + 2) < 1) {
            return false;
        }
        uint32_t bank = read_be32(ctl.data() + 4);
        if (!in(bank, 16) || read_be16(ctl.data() + bank) < 1) {
            return false;
        }
        uint32_t inst = read_be32(ctl.data() + bank + 12);
        if (!in(inst, 16) || index >= read_be16(ctl.data() + inst + 14) || !in(inst + 16 + index * 4, 4)) {
            return false;
        }
        uint32_t sound = read_be32(ctl.data() + inst + 16 + index * 4);
        if (!in(sound, 12)) {
            return false;
        }
        uint32_t wave = read_be32(ctl.data() + sound + 8);
        if (!in(wave, 20) || ctl[wave + 8] != 0) { // AL_ADPCM_WAVE
            return false;
        }
        out.base = read_be32(ctl.data() + wave);
        out.len = read_be32(ctl.data() + wave + 4);
        uint32_t loop = read_be32(ctl.data() + wave + 12);
        uint32_t book = read_be32(ctl.data() + wave + 16);
        if (!in(book, 8 + 2 * 2 * 4 * 8) || (loop != 0 && !in(loop, 12 + 32))) {
            return false;
        }
        out.book = ctl.data() + book;
        out.loop = loop != 0 ? ctl.data() + loop : nullptr;
        return true;
    }
}

void btga::config::add_bt1_sounds_option(recomp::config::Config& sound) {
    option_added = true;
    sound.add_bool_option(
        option_id,
        "BattleTanx 1 Sound Effects",
        "Plays the first BattleTanx game's versions of the sound effects the two games share. "
        "This game has smaller, duller copies of them.<br/><br/>"
        "First press Load BattleTanx ROM and choose the first BattleTanx game's ROM (USA). "
        "Its sound effects are kept, so this is only needed once.<br/><br/>"
        "Takes effect the next time the game starts.",
        false
    );
    recompui::config::add_option_button(option_id, "Load BattleTanx ROM", load_bt1);
}

// The game entry's on_init_callback: after the ROM is loaded, before the game
// runs.
void btga::apply_bt1_sounds(uint8_t* rdram, recomp_context*) {
    // Off online (src/netplay/settings.h): every PC's game needs the same sounds.
    if (!enabled() || btga::netplay::session_active()) {
        return;
    }
    std::vector<uint8_t> saved;
    {
        std::ifstream file(saved_path(), std::ios::binary);
        if (!file.good()) {
            return;
        }
        saved.assign(std::istreambuf_iterator<char>(file), {});
    }
    if (saved.size() != sizeof(magic) + bt1_ctl_size + bt1_tbl_size || std::memcmp(saved.data(), magic, sizeof(magic)) != 0) {
        return;
    }
    const std::vector<uint8_t> ctl(saved.begin() + sizeof(magic), saved.begin() + sizeof(magic) + bt1_ctl_size);
    std::span<const uint8_t> loaded = recomp::get_rom();
    if (loaded.size() < ga_wave_bank || std::memcmp(loaded.data() + ga_ptr_bank, "N64 PtrTablesV2", 15) != 0) {
        return;
    }

    std::vector<uint8_t> rom(loaded.begin(), loaded.end());
    const uint32_t new_tbl = (uint32_t)((rom.size() + 0xFFFF) & ~size_t(0xFFFF));
    rom.resize(new_tbl);
    rom.insert(rom.end(), saved.begin() + sizeof(magic) + bt1_ctl_size, saved.end());
    rom.resize((rom.size() + 0xFFFF) & ~size_t(0xFFFF));

    uint8_t* bank = rom.data() + ga_ptr_bank;
    const uint32_t count = read_be32(bank + 0x20);
    const uint32_t basenotes = read_be32(bank + 0x24);
    const uint32_t waves = read_be32(bank + 0x2C);
    if (count != ga_samples) {
        return;
    }
    for (const Replacement& r : kReplacements) {
        Bt1Wave bt;
        if (!find_bt1_wave(ctl, r.bt1, bt) || bt.base + bt.len > bt1_tbl_size || (r.skip_frames + fade_frames) * 9 >= bt.len) {
            continue;
        }
        uint8_t* wave = bank + read_be32(bank + waves + r.ga * 4);
        const uint32_t loop = read_be32(wave + 12);
        uint8_t* book = bank + read_be32(wave + 16);
        // Same book shape (order 2, 4 predictors), and a looping sample only
        // replaced by a looping one.
        if (read_be32(book) != read_be32(bt.book) || read_be32(book + 4) != read_be32(bt.book + 4) ||
            read_be32(book) != 2 || read_be32(book + 4) != 4 || ((loop != 0) && (bt.loop == nullptr))) {
            continue;
        }
        const uint32_t skip = (uint32_t)r.skip_frames * 9;
        if (r.skip_frames != 0) {
            fade_in(rom.data() + new_tbl + bt.base, r.skip_frames, bt.book);
        }
        write_be32(wave, new_tbl + bt.base + skip - ga_wave_bank);
        write_be32(wave + 4, bt.len - skip);
        std::memcpy(book + 8, bt.book + 8, 2 * 2 * 4 * 8);
        if (loop != 0) {
            std::memcpy(bank + loop, bt.loop, 12 + 32);
        }
        bank[basenotes + r.ga] = (uint8_t)((int8_t)bank[basenotes + r.ga] + r.semitones);
        applied_semitones[r.ga] = r.semitones;
    }
    recomp::set_rom_contents(std::move(rom));
}

int btga::bt1_sounds_semitones(uint32_t effect) {
    return (effect < ga_samples) ? applied_semitones[effect] : 0;
}
