// Add to Steam (the launcher's option; from the Rage Wars port's round 49): adds this copy of the game to the
// player's Steam library as a non-Steam game.
//
// Steam keeps non-Steam games in userdata/<account>/config/shortcuts.vdf, a
// binary VDF file: nested maps of typed key/value pairs. Steam reads it when
// it starts and writes its own copy back when it exits, so it's only changed
// while Steam is closed. The shortcut's app ID (which names its artwork in
// config/grid/) is the CRC-32 of its Exe and AppName fields with the top bit
// set, as Steam computes it for shortcuts added through its own menu.
#include "btga_steam.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#include <urlmon.h>
#else
#include <csignal>
#include <unistd.h>
#endif

#include "SDL.h"

// stb_image's implementation is in RT64; the writer is only used here.
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace fs = std::filesystem;

namespace {
    const char* const kAppName = "BattleTanx: Global Assault Recompiled";

    // Steam's artwork for the shortcut, from SteamGridDB (BattleTanx: Global
    // Assault, plain ones without a console's branding): a square cover and a
    // wide banner, downloaded only when the player adds the game, straight
    // into their Steam folder: none of it is part of this project. Each of
    // Steam's pieces is made from one of them at its own size (see compose())
    // and saved in config/grid/ as <app ID><suffix>.jpg. Empty: not chosen
    // yet, so no artwork (Steam shows its plain placeholder).
    const char* const kArtUrls[] = {
        "", // square cover
        "", // wide, 920x430
    };
    struct ArtPiece {
        const char* suffix;
        int source; // index in kArtUrls
        int width;
        int height;
    };
    const ArtPiece kArt[] = {
        { "p", 0, 600, 900 },       // capsule (portrait cover)
        { "", 1, 920, 430 },        // wide capsule
        { "_hero", 1, 1920, 620 },  // banner across the game's page
    };

    // --- Binary VDF ----------------------------------------------------------

    enum : uint8_t {
        kMap = 0x00,
        kString = 0x01,
        kInt32 = 0x02,
        kFloat = 0x03,
        kPointer = 0x04,
        kColor = 0x06,
        kUint64 = 0x07,
        kEnd = 0x08,
        kInt64 = 0x0A,
    };

    struct Node {
        uint8_t type = kMap;
        std::string key;
        std::string text;            // kString
        std::array<uint8_t, 8> raw{}; // the other scalars, as stored
        std::vector<Node> children;   // kMap

        Node* find(const std::string& name) {
            for (Node& child : children) {
                if (child.key == name) {
                    return &child;
                }
            }
            return nullptr;
        }
    };

    size_t scalar_size(uint8_t type) {
        switch (type) {
            case kInt32: case kFloat: case kPointer: case kColor: return 4;
            case kUint64: case kInt64: return 8;
            default: return 0;
        }
    }

    bool read_cstring(const std::vector<uint8_t>& data, size_t& pos, std::string& out) {
        size_t end = pos;
        while (end < data.size() && data[end] != 0) {
            end++;
        }
        if (end >= data.size()) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(data.data() + pos), end - pos);
        pos = end + 1;
        return true;
    }

    // Reads a map's entries up to its end marker.
    bool read_map(const std::vector<uint8_t>& data, size_t& pos, Node& map) {
        while (pos < data.size()) {
            uint8_t type = data[pos++];
            if (type == kEnd) {
                return true;
            }
            Node node;
            node.type = type;
            if (!read_cstring(data, pos, node.key)) {
                return false;
            }
            if (type == kMap) {
                if (!read_map(data, pos, node)) {
                    return false;
                }
            }
            else if (type == kString) {
                if (!read_cstring(data, pos, node.text)) {
                    return false;
                }
            }
            else {
                size_t size = scalar_size(type);
                if (size == 0 || pos + size > data.size()) {
                    return false;
                }
                std::copy(data.begin() + pos, data.begin() + pos + size, node.raw.begin());
                pos += size;
            }
            map.children.push_back(std::move(node));
        }
        return false;
    }

    void write_map(std::vector<uint8_t>& out, const Node& map) {
        for (const Node& node : map.children) {
            out.push_back(node.type);
            out.insert(out.end(), node.key.begin(), node.key.end());
            out.push_back(0);
            if (node.type == kMap) {
                write_map(out, node);
            }
            else if (node.type == kString) {
                out.insert(out.end(), node.text.begin(), node.text.end());
                out.push_back(0);
            }
            else {
                size_t size = scalar_size(node.type);
                out.insert(out.end(), node.raw.begin(), node.raw.begin() + size);
            }
        }
        out.push_back(kEnd);
    }

    Node string_node(const std::string& key, const std::string& value) {
        Node node;
        node.type = kString;
        node.key = key;
        node.text = value;
        return node;
    }

    Node int_node(const std::string& key, uint32_t value) {
        Node node;
        node.type = kInt32;
        node.key = key;
        for (int i = 0; i < 4; i++) {
            node.raw[i] = (uint8_t)(value >> (8 * i)); // little-endian
        }
        return node;
    }

    uint32_t crc32(const std::string& text) {
        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char c : text) {
            crc ^= c;
            for (int bit = 0; bit < 8; bit++) {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        return ~crc;
    }

    // --- This copy and Steam ---------------------------------------------------

    fs::path this_exe() {
#if defined(_WIN32)
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
            if (length < buffer.size()) {
                buffer.resize(length);
                return fs::path(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
#else
        // The AppImage itself, not the program inside it (mounted at a
        // different place each run).
        if (const char* appimage = std::getenv("APPIMAGE")) {
            return fs::path(appimage);
        }
        std::error_code ec;
        return fs::read_symlink("/proc/self/exe", ec);
#endif
    }

    // A path as UTF-8 text (what Steam's files hold).
    std::string utf8(const fs::path& path) {
        std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::string quoted(const fs::path& path) {
        return "\"" + utf8(path) + "\"";
    }

    bool numeric(const std::string& name) {
        return !name.empty() && name.find_first_not_of("0123456789") == std::string::npos;
    }

#if defined(_WIN32)
    std::wstring registry_string(const wchar_t* key, const wchar_t* value) {
        wchar_t buffer[1024];
        DWORD size = sizeof(buffer);
        if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
            return {};
        }
        return buffer;
    }

    DWORD registry_dword(const wchar_t* key, const wchar_t* value) {
        DWORD data = 0;
        DWORD size = sizeof(data);
        if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size) != ERROR_SUCCESS) {
            return 0;
        }
        return data;
    }
#endif

    std::vector<uint8_t> read_file(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // Downloads `url` (empty on failure).
    std::vector<uint8_t> download(const std::string& url) {
        std::error_code ec;
        fs::path temp = fs::temp_directory_path(ec) / ("btga-art-" + std::to_string(std::hash<std::string>{}(url)));
#if defined(_WIN32)
        std::wstring wide_url(url.begin(), url.end());
        if (URLDownloadToFileW(nullptr, wide_url.c_str(), temp.c_str(), 0, nullptr) != S_OK) {
            return {};
        }
#else
        std::string command = "curl -fsSL --max-time 30 -o '" + utf8(temp) + "' '" + url + "' >/dev/null 2>&1";
        if (std::system(command.c_str()) != 0) {
            return {};
        }
#endif
        std::vector<uint8_t> data = read_file(temp);
        fs::remove(temp, ec);
        return data;
    }

    struct Image {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgb;
    };

    // Bilinear scale of `src` to w x h.
    Image scaled(const Image& src, int w, int h) {
        Image out{ w, h, std::vector<uint8_t>((size_t)w * h * 3) };
        for (int y = 0; y < h; y++) {
            float sy = std::max(0.0f, (y + 0.5f) * src.height / h - 0.5f);
            int y0 = std::min((int)sy, src.height - 1), y1 = std::min(y0 + 1, src.height - 1);
            float fy = sy - y0;
            for (int x = 0; x < w; x++) {
                float sx = std::max(0.0f, (x + 0.5f) * src.width / w - 0.5f);
                int x0 = std::min((int)sx, src.width - 1), x1 = std::min(x0 + 1, src.width - 1);
                float fx = sx - x0;
                for (int c = 0; c < 3; c++) {
                    auto at = [&](int px, int py) { return (float)src.rgb[((size_t)py * src.width + px) * 3 + c]; };
                    float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
                    float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
                    out.rgb[((size_t)y * w + x) * 3 + c] = (uint8_t)(top + (bottom - top) * fy + 0.5f);
                }
            }
        }
        return out;
    }

    // `src` shrunk to w x h by averaging the pixels each new one covers.
    Image shrunk(const Image& src, int w, int h) {
        Image out{ w, h, std::vector<uint8_t>((size_t)w * h * 3) };
        for (int y = 0; y < h; y++) {
            int y0 = y * src.height / h, y1 = std::max(y0 + 1, (y + 1) * src.height / h);
            for (int x = 0; x < w; x++) {
                int x0 = x * src.width / w, x1 = std::max(x0 + 1, (x + 1) * src.width / w);
                for (int c = 0; c < 3; c++) {
                    uint64_t sum = 0;
                    for (int sy = y0; sy < y1; sy++) {
                        for (int sx = x0; sx < x1; sx++) {
                            sum += src.rgb[((size_t)sy * src.width + sx) * 3 + c];
                        }
                    }
                    out.rgb[((size_t)y * w + x) * 3 + c] = (uint8_t)(sum / ((uint64_t)(y1 - y0) * (x1 - x0)));
                }
            }
        }
        return out;
    }

    // A 3x3 box blur, twice.
    Image softened(Image image) {
        for (int pass = 0; pass < 2; pass++) {
            Image out = image;
            for (int y = 0; y < image.height; y++) {
                for (int x = 0; x < image.width; x++) {
                    for (int c = 0; c < 3; c++) {
                        int sum = 0, count = 0;
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                int px = x + dx, py = y + dy;
                                if (px >= 0 && py >= 0 && px < image.width && py < image.height) {
                                    sum += image.rgb[((size_t)py * image.width + px) * 3 + c];
                                    count++;
                                }
                            }
                        }
                        out.rgb[((size_t)y * image.width + x) * 3 + c] = (uint8_t)(sum / count);
                    }
                }
            }
            image = std::move(out);
        }
        return image;
    }

    // One of Steam's pieces from a picture of another shape: the picture whole
    // in the middle, over a blurred and darkened copy of it filling the rest.
    Image compose(const Image& cover, int w, int h) {
        float fill = std::max((float)w / cover.width, (float)h / cover.height);
        int fill_w = (int)(cover.width * fill + 0.5f), fill_h = (int)(cover.height * fill + 0.5f);
        // Blurred: shrunk to a few dozen pixels (averaging), softened, and
        // scaled back up.
        Image back = scaled(softened(shrunk(cover, std::max(1, fill_w / 24), std::max(1, fill_h / 24))), fill_w, fill_h);
        Image out{ w, h, std::vector<uint8_t>((size_t)w * h * 3) };
        int ox = (fill_w - w) / 2, oy = (fill_h - h) / 2;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < 3; c++) {
                    out.rgb[((size_t)y * w + x) * 3 + c] = back.rgb[((size_t)(y + oy) * fill_w + (x + ox)) * 3 + c] * 2 / 5;
                }
            }
        }
        float fit = std::min((float)w / cover.width, (float)h / cover.height);
        Image front = scaled(cover, (int)(cover.width * fit + 0.5f), (int)(cover.height * fit + 0.5f));
        int fx = (w - front.width) / 2, fy = (h - front.height) / 2;
        for (int y = 0; y < front.height; y++) {
            std::copy_n(&front.rgb[(size_t)y * front.width * 3], (size_t)front.width * 3, &out.rgb[((size_t)(y + fy) * w + fx) * 3]);
        }
        return out;
    }

    void append_bytes(void* context, void* data, int size) {
        auto* out = static_cast<std::vector<uint8_t>*>(context);
        out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    }
}

std::optional<btga::steam::Location> btga::steam::locate() {
    std::vector<fs::path> candidates;
#if defined(_WIN32)
    std::wstring registered = registry_string(L"Software\\Valve\\Steam", L"SteamPath");
    if (!registered.empty()) {
        candidates.emplace_back(registered);
    }
    candidates.emplace_back(L"C:\\Program Files (x86)\\Steam");
#else
    if (const char* home = std::getenv("HOME")) {
        candidates.emplace_back(fs::path(home) / ".steam/steam");
        candidates.emplace_back(fs::path(home) / ".local/share/Steam");
        candidates.emplace_back(fs::path(home) / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
    }
#endif
    std::error_code ec;
    for (const fs::path& root : candidates) {
        fs::path userdata = root / "userdata";
        if (!fs::is_directory(userdata, ec)) {
            continue;
        }
        Location location;
        location.root = fs::canonical(root, ec);
        if (ec) {
            location.root = root;
        }
        for (const auto& entry : fs::directory_iterator(userdata, ec)) {
            std::string name = utf8(entry.path().filename());
            // "0" is a placeholder folder some installs have.
            if (entry.is_directory() && numeric(name) && name != "0") {
                location.accounts.push_back(name);
            }
        }
        if (!location.accounts.empty()) {
            return location;
        }
    }
    return std::nullopt;
}

bool btga::steam::started_from_steam() {
    return std::getenv("SteamGameId") != nullptr || std::getenv("SteamAppId") != nullptr;
}

bool btga::steam::steam_running() {
#if defined(_WIN32)
    DWORD pid = registry_dword(L"Software\\Valve\\Steam\\ActiveProcess", L"pid");
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    DWORD code = 0;
    bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
#else
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return false;
    }
    std::ifstream pid_file(fs::path(home) / ".steam/steam.pid");
    long pid = 0;
    if (!(pid_file >> pid) || pid <= 0) {
        return false;
    }
    return kill((pid_t)pid, 0) == 0;
#endif
}

bool btga::steam::close_steam_and_wait(int seconds) {
    SDL_OpenURL("steam://exit");
    for (int i = 0; i < seconds * 4; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!steam_running()) {
            // Give it a moment to finish writing its files.
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return true;
        }
    }
    return false;
}

void btga::steam::open_steam() {
    SDL_OpenURL("steam://open/games");
}

btga::steam::Result btga::steam::add_shortcut(const Location& where) {
    Result result;
    fs::path exe = this_exe();
    if (exe.empty()) {
        result.error = "Couldn't find this program's own file.";
        return result;
    }
    std::string exe_field = quoted(exe);
    std::string start_dir = quoted(exe.parent_path());
    uint32_t app_id = crc32(exe_field + kAppName) | 0x80000000u;

    struct Picture {
        std::string name;
        std::vector<uint8_t> data;
    };
    std::vector<Picture> pictures;
    std::vector<Image> sources;
    for (const char* url : kArtUrls) {
        std::vector<uint8_t> file = url[0] != '\0' ? download(url) : std::vector<uint8_t>{};
        int w = 0, h = 0, channels = 0;
        uint8_t* pixels = file.empty() ? nullptr : stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &channels, 3);
        if (pixels == nullptr) {
            sources.push_back({});
            continue;
        }
        sources.push_back({ w, h, std::vector<uint8_t>(pixels, pixels + (size_t)w * h * 3) });
        stbi_image_free(pixels);
    }
    for (const ArtPiece& piece : kArt) {
        const Image& source = sources[piece.source];
        if (source.rgb.empty()) {
            continue;
        }
        Image image = compose(source, piece.width, piece.height);
        Picture picture{ std::to_string(app_id) + piece.suffix + ".jpg", {} };
        if (stbi_write_jpg_to_func(append_bytes, &picture.data, image.width, image.height, 3, image.rgb.data(), 92)) {
            pictures.push_back(std::move(picture));
        }
    }

    for (const std::string& account : where.accounts) {
        fs::path config = where.root / "userdata" / account / "config";
        fs::path file = config / "shortcuts.vdf";
        std::error_code ec;
        fs::create_directories(config, ec);

        Node root;
        Node* shortcuts = nullptr;
        if (fs::exists(file, ec)) {
            std::vector<uint8_t> data = read_file(file);
            size_t pos = 0;
            if (!data.empty() && !read_map(data, pos, root)) {
                result.error = "Steam's shortcut list (" + utf8(file) + ") couldn't be read, so it was left alone.";
                return result;
            }
            fs::copy_file(file, config / "shortcuts.vdf.btga-backup", fs::copy_options::overwrite_existing, ec);
        }
        shortcuts = root.find("shortcuts");
        if (shortcuts == nullptr) {
            Node map;
            map.key = "shortcuts";
            root.children.push_back(std::move(map));
            shortcuts = &root.children.back();
        }

        // This copy's entry if it's there already (same program file).
        Node* entry = nullptr;
        for (Node& existing : shortcuts->children) {
            Node* exe_node = existing.find("Exe");
            if (exe_node == nullptr) {
                exe_node = existing.find("exe");
            }
            if (exe_node != nullptr && exe_node->text == exe_field) {
                entry = &existing;
                break;
            }
        }
        bool added = entry == nullptr;
        if (added) {
            Node map;
            map.key = std::to_string(shortcuts->children.size());
            shortcuts->children.push_back(std::move(map));
            entry = &shortcuts->children.back();
        }

        Node tags;
        tags.key = "tags";
        entry->children = {
            int_node("appid", app_id),
            string_node("AppName", kAppName),
            string_node("Exe", exe_field),
            string_node("StartDir", start_dir),
            string_node("icon", utf8(exe)),
            string_node("ShortcutPath", ""),
            string_node("LaunchOptions", ""),
            int_node("IsHidden", 0),
            int_node("AllowDesktopConfig", 1),
            int_node("AllowOverlay", 1),
            int_node("OpenVR", 0),
            int_node("Devkit", 0),
            string_node("DevkitGameID", ""),
            int_node("DevkitOverrideAppID", 0),
            int_node("LastPlayTime", 0),
            string_node("FlatpakAppID", ""),
            tags,
        };

        std::vector<uint8_t> out;
        write_map(out, root);
        fs::path temp = config / "shortcuts.vdf.btga-new";
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
            if (!stream) {
                result.error = "Couldn't write " + utf8(temp) + ".";
                return result;
            }
        }
        fs::rename(temp, file, ec);
        if (ec) {
            result.error = "Couldn't replace " + utf8(file) + ": " + ec.message();
            return result;
        }
        result.added = result.added || added;
        result.accounts++;

        fs::path grid = config / "grid";
        fs::create_directories(grid, ec);
        int saved = 0;
        for (const Picture& picture : pictures) {
            std::ofstream stream(grid / picture.name, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(picture.data.data()), (std::streamsize)picture.data.size());
            if (stream) {
                saved++;
            }
        }
        result.art = saved;
    }
    result.ok = true;
    return result;
}
