#ifndef __BTGA_NETPLAY_MESSAGES_H__
#define __BTGA_NETPLAY_MESSAGES_H__

#include <cstddef>
#include <string>
#include <string_view>

// What netplay tells the players (messages.cpp): a line over the game for a
// while, and the chat. For now they go to the log; the Netplay screen (step
// 3) shows them.
namespace btga::netplay {
    // A message for `seconds` (0 until the next one; "" clears it).
    void show_message(const std::string& text, int seconds);
    // Each controller sweep.
    void message_tick();

    constexpr size_t kChatMaxLength = 255;
    // A player's message (any thread). `port` 0 is player 1. Shown as
    // chat_sanitize leaves it.
    void chat_add_line(int port, std::string_view text);
    // Each controller sweep.
    void chat_tick();
    // Printable ASCII only (no line breaks, markup is shown as text), at most
    // kChatMaxLength, trimmed.
    std::string chat_sanitize(std::string_view text);
}

#endif
