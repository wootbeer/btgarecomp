// The update check (update_check.h): GitHub's latest release of the port, its
// tag ("v0.1.12") compared with this build's version, number by number. Only
// the tag is read from the answer, as text. From PaxDino's.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "http.h"
#include "update_check.h"

namespace {
    std::string current_version;
    std::mutex mutex;
    std::string found; // under mutex
    std::atomic<bool> started{ false };

    // "v0.1.12-beta" -> 0, 1, 12: the first run of digits and dots.
    std::vector<int> numbers(const std::string& text) {
        std::vector<int> out;
        size_t i = 0;
        while (i < text.size() && !std::isdigit((unsigned char)text[i])) {
            i++;
        }
        while (i < text.size() && out.size() < 4) {
            size_t start = i;
            while (i < text.size() && std::isdigit((unsigned char)text[i]) && i - start < 6) {
                i++;
            }
            if (i == start) {
                break;
            }
            out.push_back(std::atoi(text.substr(start, i - start).c_str()));
            if (i >= text.size() || text[i] != '.') {
                break;
            }
            i++;
        }
        return out;
    }

    bool is_newer(const std::vector<int>& a, const std::vector<int>& b) {
        for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
            int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
            if (x != y) {
                return x > y;
            }
        }
        return false;
    }

    // "tag_name":"v0.1.12" from GitHub's JSON, letters, digits and .-_ only.
    std::string tag_of(const std::string& json) {
        size_t at = json.find("\"tag_name\"");
        if (at == std::string::npos) {
            return "";
        }
        at = json.find('"', json.find(':', at) + 1);
        if (at == std::string::npos) {
            return "";
        }
        std::string tag;
        for (size_t i = at + 1; i < json.size() && json[i] != '"' && tag.size() < 24; i++) {
            char c = json[i];
            if (std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') {
                tag.push_back(c);
            }
        }
        return tag;
    }
}

void btga::netplay::update::set_current(const std::string& version) {
    current_version = version;
}

const std::string& btga::netplay::update::current() {
    return current_version;
}

void btga::netplay::update::check() {
    if (started.exchange(true)) {
        return;
    }
    std::string mine = current_version;
    std::thread([mine] {
        std::string body;
        if (!btga::netplay::http::get("https://api.github.com/repos/wootbeer/btgarecomp/releases/latest", body, 10)) {
            return;
        }
        std::string tag = tag_of(body);
        std::vector<int> latest = numbers(tag), ours = numbers(mine);
        if (latest.empty() || ours.empty() || !is_newer(latest, ours)) {
            return;
        }
        std::string text;
        for (size_t i = 0; i < latest.size(); i++) {
            text += (i ? "." : "") + std::to_string(latest[i]);
        }
        std::lock_guard lock{ mutex };
        found = text;
    }).detach();
}

std::string btga::netplay::update::newer() {
    std::lock_guard lock{ mutex };
    return found;
}
