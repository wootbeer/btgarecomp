// Netplay's messages and chat lines (messages.h): to the log for now.
#include <cstdio>
#include <mutex>

#include "messages.h"

namespace {
    std::mutex message_mutex;
    std::string current_message;
}

void btga::netplay::show_message(const std::string& text, int) {
    std::lock_guard lock{ message_mutex };
    if (text != current_message && !text.empty()) {
        std::fprintf(stderr, "[NET] message: %s\n", text.c_str());
    }
    current_message = text;
}

void btga::netplay::message_tick() {
}

std::string btga::netplay::chat_sanitize(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c >= 0x20 && c <= 0x7E && out.size() < kChatMaxLength) {
            out.push_back(c);
        }
    }
    const size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = out.find_last_not_of(' ');
    return out.substr(start, end - start + 1);
}

void btga::netplay::chat_add_line(int port, std::string_view text) {
    const std::string clean = chat_sanitize(text);
    if (!clean.empty()) {
        std::fprintf(stderr, "[NET] chat, player %d: %s\n", port + 1, clean.c_str());
    }
}

void btga::netplay::chat_tick() {
}
