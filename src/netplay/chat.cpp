// Netplay chat (messages.h), from the Rage Wars port's: a few lines at the
// left of the screen and a line to type in. The Chat control (Controls tab;
// T by default, any key or controller button) opens it in a session, on the
// game's menu screens, in a match and paused, not while the menu (F1) is
// open; Enter doesn't open it on the menu screens nor, for whoever drives the
// pause menu (the host), while paused: it selects there. While
// it's open every key goes to it and this PC's player gets no input: Enter
// sends, Escape clears what's typed and a second Escape closes it, Ctrl+V
// pastes; on a controller the Chat button again sends and B is Escape.
// Messages go through the host (session.cpp); each PC allows ten in any 30
// seconds, and every PC drops more than twelve of a player's in 30 seconds.
// Text is printable ASCII only, at most kChatMaxLength, and shown as text
// (recompui escapes it), never as markup.
// The keys and buttons it takes (the one that opens it, Enter that sends,
// Escape that closes, every one while it's open) are kept from the game
// until let go: the game reads their state, not their events, and Enter is
// also Start (chat_held_buttons).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "SDL.h"
#include "recompinput/input_mapping.h"
#include "recompinput/input_state.h"
#include "recompinput/input_types.h"
#include "recompinput/recompinput.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "recompui/recompui.h"
#include "elements/ui_container.h"
#include "elements/ui_label.h"

#include "messages.h"
#include "session.h"

namespace {
    using namespace recompui;
    using clock = std::chrono::steady_clock;

    constexpr size_t kShownLines = 6;   // while not typing, the newest, for kLineTime
    constexpr size_t kTypingLines = 10; // while typing
    constexpr auto kLineTime = std::chrono::seconds(12);
    constexpr size_t kRateCount = 10;   // messages in any kRateWindow
    constexpr auto kRateWindow = std::chrono::seconds(30);
    using recompinput::GameInput;
    using recompinput::InputField;

    struct Line {
        int port;        // -1: a note from the chat itself
        std::string who; // the player's name when the line came
        std::string text;
        clock::time_point when;
    };

    std::mutex chat_mutex;
    // Where it can open (chat_set_where): 0 nowhere, 1 the game's menu
    // screens and the host's pause menu (not by Enter), 2 a match.
    std::atomic<int> allowed{ 1 };
    bool open = false;
    bool swallow_open_text = false; // the letter key that opened it, still to come as text
    std::string draft;
    std::deque<Line> lines;
    std::deque<clock::time_point> sent; // this PC's, for the rate limit
    std::vector<SDL_Scancode> taken_keys; // the chat's, until let go
    std::vector<uint8_t> taken_buttons;   // the same for controller buttons

    void take_key(SDL_Scancode sc) {
        if (std::find(taken_keys.begin(), taken_keys.end(), sc) == taken_keys.end()) {
            taken_keys.push_back(sc);
        }
    }

    void take_button(uint8_t button) {
        if (std::find(taken_buttons.begin(), taken_buttons.end(), button) == taken_buttons.end()) {
            taken_buttons.push_back(button);
        }
    }

    // This PC's player's profile for a device (player 1's on this PC).
    int profile_for(recompinput::InputDevice device) {
        namespace profiles = recompinput::profiles;
        if (recompinput::players::is_single_player_mode()) {
            return device == recompinput::InputDevice::Keyboard ? profiles::get_sp_keyboard_profile_index()
                                                                : profiles::get_sp_controller_profile_index();
        }
        return profiles::get_input_profile_for_player(0, device);
    }

    // Whether `pressed` is bound to Chat.
    bool is_chat_input(const InputField& pressed) {
        for (auto device : { recompinput::InputDevice::Keyboard, recompinput::InputDevice::Controller }) {
            const int profile = profile_for(device);
            if (profile < 0) {
                continue;
            }
            for (size_t slot = 0; slot < recompinput::num_bindings_per_input; slot++) {
                if (recompinput::profiles::get_input_binding(profile, GameInput::OPEN_CHAT, slot) == pressed) {
                    return true;
                }
            }
        }
        return false;
    }

    void add_line_locked(int port, std::string text) {
        // A note replaces the one before it, if that's the last line.
        if (port < 0 && !lines.empty() && lines.back().port < 0) {
            lines.pop_back();
        }
        std::string who = port >= 0 ? btga::netplay::player_name(port) : "";
        lines.push_back(Line{ port, std::move(who), std::move(text), clock::now() });
        while (lines.size() > 50) {
            lines.pop_front();
        }
    }

    void open_chat(bool by_letter_key, bool by_controller) {
        open = true;
        swallow_open_text = by_letter_key;
        SDL_StartTextInput();
        // Steam's on-screen keyboard (Steam Deck, Big Picture), when Steam
        // started the game.
        if (by_controller && (std::getenv("SteamDeck") != nullptr || std::getenv("SteamGameId") != nullptr)) {
            SDL_OpenURL("steam://open/keyboard");
        }
    }

    void close_chat() {
        open = false;
        swallow_open_text = false;
    }

    // Escape: what's typed goes, then the line closes.
    void escape() {
        if (!draft.empty()) {
            draft.clear();
        }
        else {
            close_chat();
        }
    }

    void send() {
        const std::string text = btga::netplay::chat_sanitize(draft);
        if (text.empty()) {
            draft.clear();
            close_chat();
            return;
        }
        const clock::time_point now = clock::now();
        while (!sent.empty() && now - sent.front() >= kRateWindow) {
            sent.pop_front();
        }
        if (sent.size() >= kRateCount) {
            const auto wait = std::chrono::duration_cast<std::chrono::seconds>(kRateWindow - (now - sent.front())).count() + 1;
            add_line_locked(-1, "Too many messages. Wait " + std::to_string(wait) + (wait == 1 ? " second." : " seconds."));
            return; // still open, with what was typed
        }
        if (!btga::netplay::send_chat(text)) {
            add_line_locked(-1, "Nobody to send it to.");
        }
        else {
            sent.push_back(now);
            add_line_locked(btga::netplay::local_port(), text);
        }
        draft.clear();
        close_chat();
    }

    void type(std::string_view text) {
        for (char c : text) {
            if (draft.size() >= btga::netplay::kChatMaxLength) {
                break;
            }
            if (c >= 0x20 && c <= 0x7E) {
                draft.push_back(c);
            }
        }
    }

    // What the chat takes from SDL's queue (0: dropped, the game and menus
    // don't see it). Runs where events are pumped.
    int SDLCALL filter_events(void*, SDL_Event* event) {
        std::lock_guard lock{ chat_mutex };
        if (event->type == SDL_KEYUP) {
            taken_keys.erase(std::remove(taken_keys.begin(), taken_keys.end(), event->key.keysym.scancode), taken_keys.end());
        }
        if (event->type == SDL_CONTROLLERBUTTONUP) {
            taken_buttons.erase(std::remove(taken_buttons.begin(), taken_buttons.end(), event->cbutton.button), taken_buttons.end());
        }
        if (!btga::netplay::active()) {
            open = false;
            return 1;
        }
        // Nowhere (unused for now): the keys are the game's. A line still open
        // closes (what's typed is kept), and the key that found it so goes
        // nowhere.
        if (allowed.load() == 0) {
            if (!open) {
                return 1;
            }
            close_chat();
            if (event->type == SDL_KEYDOWN) {
                take_key(event->key.keysym.scancode);
            }
            if (event->type == SDL_CONTROLLERBUTTONDOWN) {
                take_button(event->cbutton.button);
            }
            return (event->type == SDL_KEYDOWN || event->type == SDL_TEXTINPUT || event->type == SDL_CONTROLLERBUTTONDOWN) ? 0 : 1;
        }
        switch (event->type) {
        case SDL_KEYDOWN: {
            const SDL_Scancode sc = event->key.keysym.scancode;
            if (open) {
                take_key(sc);
                if (recompinput::game_input_disabled()) {
                    close_chat(); // the menu (F1) opened over it: the menu has the keys
                    return 1;
                }
                if (sc == SDL_SCANCODE_ESCAPE) {
                    if (event->key.repeat == 0) {
                        escape();
                    }
                }
                else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                    if (event->key.repeat == 0) {
                        send();
                    }
                }
                else if (sc == SDL_SCANCODE_BACKSPACE) {
                    if (!draft.empty()) {
                        draft.pop_back();
                    }
                }
                else if (sc == SDL_SCANCODE_V && (event->key.keysym.mod & KMOD_CTRL) != 0) {
                    char* clip = SDL_GetClipboardText();
                    if (clip != nullptr) {
                        std::string text = clip;
                        SDL_free(clip);
                        std::replace(text.begin(), text.end(), '\n', ' ');
                        std::replace(text.begin(), text.end(), '\t', ' ');
                        type(text);
                    }
                }
                return 0;
            }
            const bool enter = sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER;
            const bool here = allowed.load() == 2 || !enter; // Enter selects on the menu screens
            if (here && event->key.repeat == 0 && !recompinput::game_input_disabled() && is_chat_input(InputField::keyboard(sc))) {
                take_key(sc);
                const SDL_Keycode key = SDL_GetKeyFromScancode(sc);
                open_chat(key >= 0x20 && key <= 0x7E, false);
                return 0;
            }
            return 1;
        }
        case SDL_KEYUP:
            return open ? 0 : 1;
        case SDL_TEXTINPUT:
            if (!open) {
                return 1;
            }
            // The letter of the key that opened it, on its way still.
            if (swallow_open_text && draft.empty() && event->text.text[0] != '\0' && event->text.text[1] == '\0') {
                swallow_open_text = false;
                return 0;
            }
            swallow_open_text = false;
            type(event->text.text);
            return 0;
        case SDL_CONTROLLERBUTTONDOWN: {
            const InputField pressed = InputField::controller_digital((SDL_GameControllerButton)event->cbutton.button);
            if (open) {
                take_button(event->cbutton.button);
                if (recompinput::game_input_disabled()) {
                    close_chat();
                    return 1;
                }
                if (event->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    escape();
                }
                else if (is_chat_input(pressed)) {
                    send();
                }
                return 0;
            }
            if (!recompinput::game_input_disabled() && is_chat_input(pressed)) {
                take_button(event->cbutton.button);
                open_chat(false, true);
                return 0;
            }
            return 1;
        }
        case SDL_CONTROLLERBUTTONUP:
            return open ? 0 : 1;
        default:
            return 1;
        }
    }

    // ------------------------------------------- the box over the game

    ContextId chat_context = ContextId::null();
    Element* chat_box = nullptr;
    std::vector<Label*> line_labels;
    Label* input_label = nullptr;
    std::string shown; // what the box shows now, to skip updates that change nothing

    void make_chat_overlay() {
        chat_context = create_context();
        chat_context.open();
        chat_context.set_captures_input(false);
        chat_context.set_captures_mouse(false);
        Element* root = chat_context.get_root_element();
        root->set_width(100.0f, Unit::Percent);
        root->set_height(100.0f, Unit::Percent);
        chat_box = chat_context.create_element<Container>(root, FlexDirection::Column, JustifyContent::FlexEnd);
        chat_box->set_position(Position::Absolute);
        chat_box->set_left(1.5f, Unit::Percent);
        chat_box->set_bottom(24.0f, Unit::Percent);
        chat_box->set_width(34.0f, Unit::Percent);
        chat_box->set_padding(8.0f);
        chat_box->set_gap(2.0f);
        chat_box->set_border_radius(theme::border::radius_sm);
        chat_box->set_display(Display::None);
        for (size_t i = 0; i < kTypingLines; i++) {
            Label* label = chat_context.create_element<Label>(chat_box, "", theme::Typography::LabelSM);
            label->set_white_space(WhiteSpace::Normal);
            label->set_display(Display::None);
            line_labels.push_back(label);
        }
        input_label = chat_context.create_element<Label>(chat_box, "", theme::Typography::LabelSM);
        input_label->set_white_space(WhiteSpace::Normal);
        input_label->set_color(theme::color::Primary);
        input_label->set_display(Display::None);
        chat_context.close();
        show_context(chat_context, "");
    }

    std::string line_text(const Line& line) {
        return line.port < 0 ? line.text : line.who + ": " + line.text;
    }
}

void btga::netplay::chat_install() {
    SDL_SetEventFilter(filter_events, nullptr);
}

bool btga::netplay::chat_typing() {
    std::lock_guard lock{ chat_mutex };
    return open;
}

uint16_t btga::netplay::chat_held_buttons() {
    std::vector<SDL_Scancode> keys;
    std::vector<uint8_t> pads;
    {
        std::lock_guard lock{ chat_mutex };
        if (taken_keys.empty() && taken_buttons.empty()) {
            return 0;
        }
        keys = taken_keys;
        pads = taken_buttons;
    }
#define DEFINE_INPUT(name, value, readable) { recompinput::GameInput::name, (uint16_t)value },
    static const std::pair<recompinput::GameInput, uint16_t> buttons[] = { DEFINE_N64_BUTTON_INPUTS() };
#undef DEFINE_INPUT
    uint16_t mask = 0;
    auto mask_bound = [&](int profile, const InputField& field) {
        if (profile < 0) {
            return;
        }
        for (const auto& [input, value] : buttons) {
            for (size_t slot = 0; slot < recompinput::num_bindings_per_input; slot++) {
                if (recompinput::profiles::get_input_binding(profile, input, slot) == field) {
                    mask |= value;
                }
            }
        }
    };
    const int keyboard = profile_for(recompinput::InputDevice::Keyboard);
    const int controller = profile_for(recompinput::InputDevice::Controller);
    for (SDL_Scancode sc : keys) {
        mask_bound(keyboard, InputField::keyboard(sc));
    }
    for (uint8_t button : pads) {
        mask_bound(controller, InputField::controller_digital((SDL_GameControllerButton)button));
    }
    return mask;
}

void btga::netplay::chat_set_where(int where) {
    allowed.store(where);
}

void btga::netplay::chat_set_defaults() {
    recompinput::set_game_input_description(GameInput::OPEN_CHAT,
        "Netplay: opens the chat line. "
        "Enter sends, Escape clears it and then closes it; on a controller this button sends and B closes.");
    // T; Enter is Start (pause) here. No controller button by default (its
    // buttons are all the game's), though one can be bound.
    recompinput::set_default_mapping_for_keyboard(GameInput::OPEN_CHAT, { InputField::keyboard(SDL_SCANCODE_T) });
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
    std::string clean = chat_sanitize(text);
    if (clean.empty()) {
        return;
    }
    std::lock_guard lock{ chat_mutex };
    add_line_locked(port, std::move(clean));
}

void btga::netplay::chat_tick() {
    std::vector<std::string> show;
    std::string input;
    bool typing_now;
    {
        std::lock_guard lock{ chat_mutex };
        if (open && (recompinput::game_input_disabled() || allowed.load() == 0)) {
            close_chat(); // the menu (F1) opened over it
        }
        typing_now = open;
        const clock::time_point now = clock::now();
        const size_t most = typing_now ? kTypingLines : kShownLines;
        for (auto it = lines.rbegin(); it != lines.rend() && show.size() < most; ++it) {
            if (!typing_now && now - it->when >= kLineTime) {
                break;
            }
            show.push_back(line_text(*it));
        }
        std::reverse(show.begin(), show.end());
        if (typing_now) {
            input = "Say: " + draft + "_";
            if (draft.size() >= kChatMaxLength - 30) {
                input += "  (" + std::to_string(draft.size()) + "/" + std::to_string(kChatMaxLength) + ")";
            }
        }
    }

    std::string state = typing_now ? "T" : "-";
    state += input;
    for (const std::string& line : show) {
        state += '\n' + line;
    }
    if (state == shown) {
        return;
    }
    if (chat_context == ContextId::null()) {
        if (show.empty() && !typing_now) {
            return;
        }
        make_chat_overlay();
    }
    // Hidden only when the UI shuts down (it hides every context first).
    if (!is_context_shown(chat_context)) {
        return;
    }
    shown = state;
    ContextId previous = try_close_current_context();
    chat_context.open();
    if (show.empty() && !typing_now) {
        chat_box->set_display(Display::None);
    }
    else {
        chat_box->set_display(Display::Flex);
        chat_box->set_background_color(theme::color::ModalOverlay, typing_now ? 200 : 110);
        for (size_t i = 0; i < line_labels.size(); i++) {
            if (i < show.size()) {
                line_labels[i]->set_text(show[i]);
                line_labels[i]->set_display(Display::Block);
            }
            else {
                line_labels[i]->set_display(Display::None);
            }
        }
        if (typing_now) {
            input_label->set_text(input);
            input_label->set_display(Display::Block);
        }
        else {
            input_label->set_display(Display::None);
        }
    }
    chat_context.close();
    if (previous != ContextId::null()) {
        previous.open();
    }
}
