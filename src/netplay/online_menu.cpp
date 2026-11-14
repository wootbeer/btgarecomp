// The launcher's Netplay screen (session.cpp), from the Rage Wars port's:
// host a game on a UDP port, or join one by its join code, a public game,
// or the host's address. The host's settings are the session's
// (settings.cpp), so the joiner sees them first and joins or not; once the
// host starts, every game starts. Also the messages over the game and the
// menu's Netplay tab during a session.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"
#include "recompui/config.h"
#include "recompui/recompui.h"
#include "elements/ui_button.h"
#include "elements/ui_container.h"
#include "elements/ui_label.h"
#include "elements/ui_modal.h"
#include "elements/ui_text_input.h"
#include "recompinput/players.h"
#include "ultramodern/ultramodern.hpp"

#include "btga_netplay.h"
#include "messages.h"
#include "online_menu.h"
#include "session.h"
#include "settings.h"
#include "upnp.h"

namespace {
    using namespace recompui;

    constexpr uint16_t kDefaultPort = 47100;

    Modal* modal = nullptr;
    TextInput* port_input = nullptr;
    TextInput* address_input = nullptr;
    Button* host_button = nullptr;
    Button* join_button = nullptr;
    Button* delay_button = nullptr;
    Button* start_button = nullptr; // the host: start with the players in
    Label* status_label = nullptr;
    Label* code_label = nullptr; // the host's join code, big
    Element* lobby_card = nullptr; // while hosting or joining: the code, the players, Start
    Label* players_label = nullptr;
    Label* details_label = nullptr; // the host: how players reach it, and the settings
    std::string shown_code, shown_players, shown_details;
    bool lobby_shown = false;
    // Public games: the host's choice and name; the joiner's list.
    bool public_choice = false;
    std::string lobby_name_text = "BattleTanx game";
    std::string nickname_text; // this player's name in netplay (empty: Player N)
    TextInput* nickname_input = nullptr;
    Button* public_button = nullptr;
    TextInput* name_input = nullptr;
    Element* list_box = nullptr;
    Label* list_note = nullptr;
    constexpr size_t kListShown = 4;
    std::vector<Button*> list_buttons;
    std::vector<std::string> list_codes;
    size_t list_page = 0;     // kListShown games a page
    Element* page_row = nullptr; // Previous, "Page 2 of 5", Next: only with more than a page
    Label* page_label = nullptr;
    Button* prev_button = nullptr;
    Button* next_button = nullptr;
    std::string shown_list;
    std::chrono::steady_clock::time_point last_list;
    bool list_asked = false;
    std::u8string game_id;
    std::string port_text = std::to_string(kDefaultPort);
    std::string address_text;
    uint32_t delay_choice = 0; // the host's input delay: controller reads, 0 Auto
    std::string shown_status;
    std::string host_text; // what the host is told while it waits
    bool starting = false;
    bool join_accepted = false; // the joiner pressed Join: waiting for the host to start

    // An input delay as the menus say it.
    std::string delay_text(uint32_t reads) {
        if (reads == 0) {
            return "Auto";
        }
        int ms = (int)std::lround(reads * btga::netplay::sweep_ms());
        return std::to_string(reads) + (reads == 1 ? " read" : " reads") + " (about " + std::to_string(ms) + " ms)";
    }

    // The port, address and input delay chosen last, kept across launches.
    std::filesystem::path saved_path() {
        return recomp::get_config_path() / "online.txt";
    }

    void load_saved() {
        std::ifstream in(saved_path());
        std::string port, address;
        if (std::getline(in, port) && !port.empty()) {
            port_text = port;
        }
        if (std::getline(in, address)) {
            address_text = address;
        }
        std::string delay;
        if (std::getline(in, delay) && !delay.empty()) {
            delay_choice = (uint32_t)std::clamp(std::atoi(delay.c_str()), 0, 8);
        }
        std::string listed, name;
        if (std::getline(in, listed)) {
            public_choice = listed == "1";
        }
        if (std::getline(in, name) && !name.empty()) {
            lobby_name_text = name;
        }
        std::getline(in, nickname_text);
    }

    void save() {
        std::ofstream out(saved_path());
        out << port_text << "\n" << address_text << "\n" << delay_choice << "\n" << (public_choice ? 1 : 0) << "\n" << lobby_name_text << "\n" << nickname_text << "\n";
    }

    std::string public_text() {
        return public_choice ? "Public: On" : "Public: Off";
    }

    // A lobby name as the list shows it: letters, digits, spaces and a little
    // punctuation, 24 at most (the service cleans it again, and so does each
    // game that lists it).
    std::string clean_name(const std::string& text) {
        std::string name;
        for (char c : text) {
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                (c != '\0' && std::strchr(" '!?.,:()_-", c) != nullptr);
            if (ok && name.size() < 24 && !(c == ' ' && (name.empty() || name.back() == ' '))) {
                name.push_back(c);
            }
        }
        while (!name.empty() && name.back() == ' ') {
            name.pop_back();
        }
        return name.empty() ? "BattleTanx game" : name;
    }

    bool parse_port(const std::string& text, uint16_t& port) {
        char* end = nullptr;
        long value = std::strtol(text.c_str(), &end, 10);
        if (text.empty() || *end != '\0' || value < 1024 || value > 65535) {
            return false;
        }
        port = (uint16_t)value;
        return true;
    }

    // Changes to the menu go through its own UI context; button presses come
    // in with it open, the launcher's frames and options with the launcher's.
    template <typename F>
    void in_menu_context(F&& change) {
        ContextId context = modal->modal_root_context;
        if (get_current_context() == context) {
            change();
            return;
        }
        ContextId previous = try_close_current_context();
        context.open();
        change();
        context.close();
        if (previous != ContextId::null()) {
            previous.open();
        }
    }

    void set_status(const std::string& text) {
        if (status_label == nullptr || text == shown_status) {
            return;
        }
        shown_status = text;
        in_menu_context([&] { status_label->set_text(text); });
    }

    void set_code(const std::string& text) {
        if (code_label == nullptr || text == shown_code) {
            return;
        }
        shown_code = text;
        in_menu_context([&] {
            code_label->set_text(text);
            code_label->set_display(text.empty() ? Display::None : Display::Block);
        });
    }

    void set_players(const std::string& text) {
        if (players_label == nullptr || text == shown_players) {
            return;
        }
        shown_players = text;
        in_menu_context([&] { players_label->set_text(text); });
    }

    void set_details(const std::string& text) {
        if (details_label == nullptr || text == shown_details) {
            return;
        }
        shown_details = text;
        in_menu_context([&] {
            details_label->set_text(text);
            details_label->set_display(text.empty() ? Display::None : Display::Block);
        });
    }

    // The lobby card, while hosting or joining.
    void show_lobby(bool shown) {
        if (lobby_card == nullptr || shown == lobby_shown) {
            return;
        }
        lobby_shown = shown;
        in_menu_context([&] { lobby_card->set_display(shown ? Display::Flex : Display::None); });
        if (!shown) {
            set_code("");
            set_players("");
            set_details("");
        }
    }

    void set_buttons_enabled(bool enabled) {
        in_menu_context([&] {
            host_button->set_enabled(enabled);
            join_button->set_enabled(enabled);
            delay_button->set_enabled(enabled);
            public_button->set_enabled(enabled);
            nickname_input->set_enabled(enabled);
            list_box->set_display(enabled ? Display::Flex : Display::None);
            if (enabled) {
                start_button->set_enabled(false); // back to idle
                join_accepted = false;
            }
        });
        show_lobby(!enabled);
    }

    void use_session_menu();

    void start_session() {
        starting = true;
        btga::netplay::begin();
        // This PC's controls are its own player's (netplay puts them on its port).
        recompinput::players::set_single_player_mode(true);
        use_session_menu();
        if (modal != nullptr && modal->is_open_now()) {
            modal->close();
        }
        recomp::start_game(game_id, {});
        recompui::hide_all_contexts(); // as the launcher's Start Game
    }

    // The host's settings for everyone, on one line.
    std::string settings_line(const std::string& described) {
        std::string line;
        size_t start = 0;
        while (start < described.size()) {
            size_t end = described.find('\n', start);
            if (end == std::string::npos) {
                end = described.size();
            }
            if (end > start) {
                line += (line.empty() ? "" : ", ") + described.substr(start, end - start);
            }
            start = end + 1;
        }
        return line.empty() ? "the defaults" : line;
    }

    // Under the host's players: how others reach it, the delay, the settings.
    std::string make_host_text() {
        std::string lan = btga::netplay::lan_address();
        std::string text;
        if (!lan.empty()) {
            text += "Same network: join " + lan + ":" + port_text + "\n";
        }
        text += "Over the internet without the code: your public IP, UDP port " + port_text + " forwarded to this PC\n";
        text += "Input delay: " + (delay_choice == 0 ? std::string("Auto (from the pings when you start)") : delay_text(delay_choice)) + "\n";
        text += "Settings for everyone: " + settings_line(btga::netplay::describe_settings(btga::netplay::own_settings()));
        return text;
    }

    void on_host() {
        uint16_t port;
        if (!parse_port(port_text, port)) {
            set_status("The port must be a number from 1024 to 65535.");
            return;
        }
        save();
        btga::netplay::set_nickname(nickname_text);
        btga::netplay::set_delay_choice(delay_choice);
        btga::netplay::set_public(public_choice, clean_name(lobby_name_text));
        std::string error;
        if (!btga::netplay::host(port, error)) {
            set_status(error);
            return;
        }
        host_text = make_host_text();
        set_buttons_enabled(false);
        in_menu_context([] { start_button->set_display(Display::Flex); });
    }

    void on_join() {
        btga::netplay::set_nickname(nickname_text);
        std::string text = address_text;
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        while (!text.empty() && text.front() == ' ') {
            text.erase(text.begin());
        }
        // A join code.
        if (btga::netplay::looks_like_code(text)) {
            save();
            std::string error;
            if (!btga::netplay::join_by_code(text, error)) {
                set_status(error);
                return;
            }
            set_buttons_enabled(false);
            in_menu_context([] { start_button->set_display(Display::None); }); // the host's
            set_players("Looking up join code " + text + "...");
            return;
        }
        // address:port, or the address alone on the port above.
        std::string address = text;
        std::string port_part = port_text;
        if (size_t colon = text.rfind(':'); colon != std::string::npos && text.find(':') == colon) {
            address = text.substr(0, colon);
            port_part = text.substr(colon + 1);
        }
        uint16_t port;
        if (address.empty()) {
            set_status("Type the host's join code, or their address (their IP address, and :port if it isn't the one above).");
            return;
        }
        if (!parse_port(port_part, port)) {
            set_status("The port must be a number from 1024 to 65535.");
            return;
        }
        save();
        std::string error;
        if (!btga::netplay::join(address, port, error)) {
            set_status(error);
            return;
        }
        set_buttons_enabled(false);
        in_menu_context([] { start_button->set_display(Display::None); }); // the host's
        set_players("Contacting " + address + " on port " + std::to_string(port) + "...");
    }

    // Text sizes: readable from a couch, and the whole screen still fits one
    // window height (the UI scales with it, 1080 lines the reference).
    constexpr float kTextSize = 26.0f;  // descriptions and lists
    constexpr float kSmallSize = 22.0f; // hints and details

    Label* text_label(ContextId context, Element* parent, const std::string& text, float size, theme::color color = theme::color::Text) {
        Label* label = context.create_element<Label>(parent, text, theme::Typography::Body);
        label->set_font_size(size);
        label->set_line_height(size * 1.35f);
        label->set_color(color);
        label->set_white_space(WhiteSpace::Preline);
        return label;
    }

    // A panel: a title, then its rows.
    Element* card(ContextId context, Element* parent, const std::string& title) {
        Element* box = context.create_element<Container>(parent, FlexDirection::Column, JustifyContent::FlexStart);
        box->set_padding(20.0f);
        box->set_gap(14.0f);
        box->set_border_radius(theme::border::radius_md);
        box->set_background_color(theme::color::Background2);
        box->set_border_width(theme::border::width);
        box->set_border_color(theme::color::BorderSoft);
        box->set_as_navigation_container(NavigationType::Vertical);
        if (!title.empty()) {
            context.create_element<Label>(box, title, theme::Typography::Header3);
        }
        return box;
    }

    Element* row(ContextId context, Element* parent) {
        Element* r = context.create_element<Container>(parent, FlexDirection::Row, JustifyContent::FlexStart);
        r->set_gap(16.0f);
        r->set_align_items(AlignItems::Center);
        r->set_as_navigation_container(NavigationType::Horizontal);
        return r;
    }

    void create_modal() {
        modal = Modal::create_modal(ModalType::Fullscreen);
        ContextId context = modal->modal_root_context;
        context.open();

        Element* header_left = modal->get_header()->get_left();
        header_left->set_padding(16.0f);
        header_left->set_padding_left(20.0f);
        context.create_element<Label>(header_left, "Netplay", theme::Typography::Header3);

        Element* body = modal->get_body();
        Element* column = context.create_element<Container>(body, FlexDirection::Column, JustifyContent::FlexStart);
        column->set_padding(24.0f);
        column->set_gap(16.0f);
        column->set_width(100.0f, Unit::Percent);
        column->set_as_navigation_container(NavigationType::Vertical);

        // Who this player is to the others, and what netplay is.
        Element* name_row = row(context, column);
        Label* name_label = context.create_element<Label>(name_row, "Your name", theme::Typography::LabelMD);
        name_label->set_white_space(WhiteSpace::Nowrap);
        name_label->set_flex_shrink(0.0f);
        nickname_input = context.create_element<TextInput>(name_row);
        nickname_input->set_width(260.0f);
        nickname_input->set_flex_shrink(0.0f);
        nickname_input->set_font_size(kTextSize);
        nickname_input->set_text(nickname_text);
        nickname_input->add_text_changed_callback([](const std::string& text) { nickname_text = text; });
        text_label(context, name_row,
            "2 to 4 players on their own PCs, by the host's game settings, each with their own controls. "
            "Online, sound packs and other mods are off and nobody saves.",
            kSmallSize, theme::color::TextDim);

        // Host and Join, side by side.
        Element* cards = context.create_element<Container>(column, FlexDirection::Row, JustifyContent::FlexStart);
        cards->set_gap(20.0f);
        cards->set_width(100.0f, Unit::Percent);
        cards->set_flex_wrap(FlexWrap::Wrap); // one above the other on a narrow window
        cards->set_as_navigation_container(NavigationType::Horizontal);

        Element* host_card = card(context, cards, "Host a game");
        host_card->set_flex(1.0f, 1.0f, 0.0f);
        host_card->set_min_width(520.0f);
        Element* host_row = row(context, host_card);
        context.create_element<Label>(host_row, "UDP port", theme::Typography::LabelMD);
        port_input = context.create_element<TextInput>(host_row);
        port_input->set_width(170.0f);
        port_input->set_font_size(kTextSize);
        port_input->set_text(port_text);
        port_input->add_text_changed_callback([](const std::string& text) { port_text = text; });
        host_button = context.create_element<Button>(host_row, "Host", ButtonStyle::Primary, ButtonSize::Large);
        host_button->add_pressed_callback(on_host);
        // How far ahead each player's input is sent: Auto picks the least the
        // connection needs; more is steadier on a jumpy one, less is quicker.
        Element* delay_row = row(context, host_card);
        context.create_element<Label>(delay_row, "Input delay", theme::Typography::LabelMD);
        delay_button = context.create_element<Button>(delay_row, delay_text(delay_choice), ButtonStyle::Secondary, ButtonSize::Large);
        delay_button->add_pressed_callback([] {
            delay_choice = (delay_choice + 1) % 9;
            delay_button->set_text(delay_text(delay_choice));
            save();
        });
        text_label(context, host_card, "Auto suits most connections. Raise it if the game keeps waiting for a player.",
            kSmallSize, theme::color::TextDim);
        // Listed for anyone to join (Join a game's list), under a name.
        Element* public_row = row(context, host_card);
        public_button = context.create_element<Button>(public_row, public_text(), ButtonStyle::Secondary, ButtonSize::Large);
        public_button->add_pressed_callback([] {
            public_choice = !public_choice;
            public_button->set_text(public_text());
            save();
        });
        name_input = context.create_element<TextInput>(public_row);
        name_input->set_flex(1.0f, 1.0f, 0.0f);
        name_input->set_min_width(160.0f);
        name_input->set_font_size(kTextSize);
        name_input->set_text(lobby_name_text);
        name_input->add_text_changed_callback([](const std::string& text) { lobby_name_text = text; });

        Element* join_card = card(context, cards, "Join a game");
        join_card->set_flex(1.0f, 1.0f, 0.0f);
        join_card->set_min_width(520.0f);
        Element* join_row = row(context, join_card);
        address_input = context.create_element<TextInput>(join_row);
        address_input->set_flex(1.0f, 1.0f, 0.0f);
        address_input->set_min_width(200.0f);
        address_input->set_font_size(kTextSize);
        address_input->set_text(address_text);
        address_input->add_text_changed_callback([](const std::string& text) { address_text = text; });
        join_button = context.create_element<Button>(join_row, "Join", ButtonStyle::Primary, ButtonSize::Large);
        join_button->add_pressed_callback(on_join);
        text_label(context, join_card, "The host's join code, or their address (IP, or IP:port if it isn't " + std::to_string(kDefaultPort) + ").",
            kSmallSize, theme::color::TextDim);
        // Public games: one button each, which joins it.
        list_box = context.create_element<Container>(join_card, FlexDirection::Column, JustifyContent::FlexStart);
        list_box->set_gap(10.0f);
        list_box->set_as_navigation_container(NavigationType::Vertical);
        Element* list_head = row(context, list_box);
        context.create_element<Label>(list_head, "Public games", theme::Typography::LabelMD);
        Button* refresh_button = context.create_element<Button>(list_head, "Refresh", ButtonStyle::Tertiary, ButtonSize::Medium);
        refresh_button->add_pressed_callback([] {
            btga::netplay::refresh_public_games();
            last_list = std::chrono::steady_clock::now();
        });
        list_note = text_label(context, list_box, "", kSmallSize, theme::color::TextDim);
        list_codes.assign(kListShown, "");
        for (size_t i = 0; i < kListShown; i++) {
            Button* entry = context.create_element<Button>(list_box, "", ButtonStyle::Secondary, ButtonSize::Medium);
            entry->set_display(Display::None);
            entry->add_pressed_callback([i] {
                if (list_codes[i].empty()) {
                    return;
                }
                address_text = list_codes[i];
                address_input->set_text(address_text);
                on_join();
            });
            list_buttons.push_back(entry);
        }
        page_row = row(context, list_box);
        page_row->set_display(Display::None);
        prev_button = context.create_element<Button>(page_row, "Previous", ButtonStyle::Tertiary, ButtonSize::Medium);
        prev_button->add_pressed_callback([] {
            if (list_page > 0) {
                list_page--;
                shown_list.clear(); // shown again
            }
        });
        page_label = context.create_element<Label>(page_row, "", theme::Typography::LabelSM);
        next_button = context.create_element<Button>(page_row, "Next", ButtonStyle::Tertiary, ButtonSize::Medium);
        next_button->add_pressed_callback([] {
            list_page++; // kept to the last page when shown
            shown_list.clear();
        });

        // Messages (a failed join, a bad port...), shown in any state.
        status_label = text_label(context, column, "", kTextSize, theme::color::Warning);

        // The lobby: the code, who's in, Start and Cancel.
        // Two columns, so four players and the details still fit one screen.
        lobby_card = card(context, column, "");
        lobby_card->set_display(Display::None);
        Element* lobby_columns = context.create_element<Container>(lobby_card, FlexDirection::Row, JustifyContent::FlexStart);
        lobby_columns->set_gap(24.0f);
        lobby_columns->set_align_items(AlignItems::FlexStart);
        lobby_columns->set_as_navigation_container(NavigationType::Horizontal);
        Element* lobby_left = context.create_element<Container>(lobby_columns, FlexDirection::Column, JustifyContent::FlexStart);
        lobby_left->set_flex(1.0f, 1.0f, 0.0f);
        lobby_left->set_gap(16.0f);
        lobby_left->set_as_navigation_container(NavigationType::Vertical);
        Element* lobby_right = context.create_element<Container>(lobby_columns, FlexDirection::Column, JustifyContent::FlexStart);
        lobby_right->set_flex(1.0f, 1.0f, 0.0f);
        code_label = context.create_element<Label>(lobby_left, "", theme::Typography::Header2);
        code_label->set_display(Display::None);
        players_label = text_label(context, lobby_left, "", kTextSize);
        Element* cancel_row = row(context, lobby_left);
        start_button = context.create_element<Button>(cancel_row, "Start", ButtonStyle::Success, ButtonSize::Large);
        start_button->set_enabled(false);
        start_button->add_pressed_callback([] {
            if (btga::netplay::host_start()) {
                start_session();
            }
        });
        Button* cancel_button = context.create_element<Button>(cancel_row, "Cancel", ButtonStyle::Tertiary, ButtonSize::Large);
        cancel_button->add_pressed_callback([] {
            btga::netplay::cancel();
            set_buttons_enabled(true);
            set_status("");
        });
        details_label = text_label(context, lobby_right, "", kSmallSize, theme::color::TextDim);
        details_label->set_display(Display::None);

        context.close();

        // Closed (Escape, or the menu button): nobody waits on this PC.
        modal->set_on_close_callback([] {
            if (!starting) {
                set_code("");
                btga::netplay::cancel();
                set_buttons_enabled(true);
                set_status("");
            }
        });
    }
}

void btga::netplay::open_screen(const std::u8string& id) {
    game_id = id;
    // The runtime's list of valid ROMs is made at startup: a ROM loaded since
    // (Load ROM) is stored but not on it until checked again.
    if (!recomp::is_rom_valid(game_id)) {
        recomp::check_all_stored_roms();
    }
    if (!recomp::is_rom_valid(game_id)) {
        recompui::open_info_prompt("Netplay", "Load the game's ROM first (Load ROM).", "OK", nullptr);
        return;
    }
    if (modal == nullptr) {
        load_saved();
        ContextId previous = try_close_current_context();
        create_modal();
        if (previous != ContextId::null()) {
            previous.open();
        }
    }
    modal->open();
    in_menu_context([] { host_button->focus(); });
}

namespace {
    // While idle: the public games, asked for again every 15 seconds.
    void update_public_list() {
        auto now = std::chrono::steady_clock::now();
        if (!list_asked || now - last_list >= std::chrono::seconds(15)) {
            btga::netplay::refresh_public_games();
            last_list = now;
            list_asked = true;
        }
        std::vector<btga::netplay::PublicGame> games;
        btga::netplay::ListState state = btga::netplay::public_games(games);
        std::string note;
        if (state == btga::netplay::ListState::Failed) {
            note = "Couldn't get the list. Check your internet connection.";
        }
        else if (state == btga::netplay::ListState::Done && games.empty()) {
            note = "None open right now. Host one with Public on.";
        }
        else if (state != btga::netplay::ListState::Done) {
            note = "Looking...";
        }
        size_t pages = std::max<size_t>(1, (games.size() + kListShown - 1) / kListShown);
        list_page = std::min(list_page, pages - 1);
        size_t first = list_page * kListShown;
        std::string key = note + "#" + std::to_string(list_page);
        for (const auto& g : games) {
            key += "|" + g.code + g.name + std::to_string(g.players);
        }
        if (key == shown_list) {
            return;
        }
        shown_list = key;
        in_menu_context([&] {
            list_note->set_text(note);
            list_note->set_display(note.empty() ? Display::None : Display::Block);
            for (size_t i = 0; i < list_buttons.size(); i++) {
                if (first + i < games.size()) {
                    const btga::netplay::PublicGame& g = games[first + i];
                    list_codes[i] = g.code;
                    list_buttons[i]->set_text(g.name + "  (" + std::to_string(g.players) + "/4)");
                    list_buttons[i]->set_display(Display::Flex);
                }
                else {
                    list_codes[i].clear();
                    list_buttons[i]->set_display(Display::None);
                }
            }
            page_row->set_display(pages > 1 ? Display::Flex : Display::None);
            page_label->set_text("Page " + std::to_string(list_page + 1) + " of " + std::to_string(pages));
            prev_button->set_enabled(list_page > 0);
            next_button->set_enabled(list_page + 1 < pages);
        });
    }
}

namespace {
    bool open_at_start = false; // --netplay (Leave Game)
}

void btga::netplay::command_line(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--netplay") == 0) {
            open_at_start = true;
        }
    }
}

void btga::netplay::launcher_update(const std::u8string& id) {
    // A BTGA_NETPLAY session began before the menus were up.
    static bool session_menu_set = false;
    if (!session_menu_set && btga::netplay::active()) {
        session_menu_set = true;
        use_session_menu();
    }
    // Leave Game started this one again: on the Netplay screen.
    static int frames_to_screen = 30;
    if (open_at_start && --frames_to_screen <= 0) {
        open_at_start = false;
        btga::netplay::open_screen(id);
    }
    if (modal == nullptr || starting) {
        return;
    }
    if (!modal->is_open_now()) {
        return;
    }
    switch (btga::netplay::poll_lobby()) {
    case btga::netplay::Lobby::Idle:
        update_public_list();
        break;
    case btga::netplay::Lobby::Waiting:
        if (btga::netplay::local_port() == 0) {
            // Who's in, and Start once someone has accepted.
            std::string list = "Player 1: you (host)";
            int ready = 0;
            std::vector<btga::netplay::LobbyPlayer> joined = btga::netplay::lobby_players();
            for (size_t i = 0; i < joined.size(); i++) {
                list += "\nPlayer " + std::to_string(i + 2) + ": " + (joined[i].name.empty() ? "" : joined[i].name + ", ") +
                    (joined[i].ready ? "ready" : "getting ready...");
                ready += joined[i].ready ? 1 : 0;
            }
            if (joined.empty()) {
                list += "\nWaiting for players to join (up to 3)...";
            }
            set_players(list);
            in_menu_context([&] { start_button->set_enabled(ready > 0); });
            std::string code = btga::netplay::join_code();
            std::string note = btga::netplay::join_code_note();
            set_code(code.empty() ? "" : "Join code: " + code);
            std::string upnp = btga::netplay::upnp::status();
            set_details((code.empty() && !note.empty() ? note + "\n" : "") + (upnp.empty() ? "" : upnp + "\n") + host_text);
            set_status(btga::netplay::version_note_text());
        }
        else if (join_accepted) {
            set_players("You're in. Waiting for the host to start...");
        }
        break;
    case btga::netplay::Lobby::Go:
        start_session();
        break;
    case btga::netplay::Lobby::Found:
        if (!recompui::is_prompt_open()) {
            set_players("The host answered.");
            recompui::open_choice_prompt(
                "Join Netplay Game",
                "Input delay: " + (btga::netplay::host_delay_choice() == 0 ? std::string("Auto (set from the ping as you join)")
                    : delay_text(btga::netplay::host_delay_choice()) + ", the host's choice") +
                "\n\nThe host's settings, used for every player:\n" + btga::netplay::host_settings_text(),
                "Join", "Cancel",
                [] {
                    btga::netplay::accept();
                    join_accepted = true;
                },
                [] {
                    btga::netplay::cancel();
                    set_buttons_enabled(true);
                    set_status("");
                },
                recompui::ButtonStyle::Success, recompui::ButtonStyle::Tertiary, false);
        }
        break;
    case btga::netplay::Lobby::Failed:
        join_accepted = false;
        set_status(btga::netplay::lobby_error_text());
        btga::netplay::cancel();
        set_buttons_enabled(true);
        shown_status.clear(); // keep the message, but let the next one show
        break;
    }
}

// ------------------------------------------- messages over the game

namespace {
    // A line at the top of the screen, over the game, that takes no input.
    ContextId message_context = ContextId::null();
    Element* message_box = nullptr;
    Label* message_label = nullptr;
    std::mutex message_mutex;
    std::string message_shown;
    std::chrono::steady_clock::time_point message_until;

    void make_message_overlay() {
        message_context = create_context();
        message_context.open();
        message_context.set_captures_input(false);
        message_context.set_captures_mouse(false);
        Element* root = message_context.get_root_element();
        root->set_width(100.0f, Unit::Percent);
        root->set_height(100.0f, Unit::Percent);
        message_box = message_context.create_element<Element>(root);
        message_box->set_position(Position::Absolute);
        message_box->set_top(14.0f, Unit::Percent);
        message_box->set_left(50.0f, Unit::Percent);
        message_box->set_translate_2D(-50.0f, 0.0f, Unit::Percent);
        message_box->set_padding(14.0f);
        message_box->set_padding_left(24.0f);
        message_box->set_padding_right(24.0f);
        message_box->set_border_radius(theme::border::radius_md);
        message_box->set_background_color(theme::color::ModalOverlay);
        message_box->set_display(Display::None);
        message_label = message_context.create_element<Label>(message_box, "", theme::Typography::LabelMD);
        message_label->set_white_space(WhiteSpace::Preline);
        message_context.close();
        show_context(message_context, "");
    }

    void show_message_now(const std::string& text) {
        if (text == message_shown) {
            return;
        }
        if (message_context == ContextId::null()) {
            if (text.empty()) {
                return;
            }
            make_message_overlay();
        }
        // Hidden only when the UI shuts down (it hides every context first).
        if (!is_context_shown(message_context)) {
            return;
        }
        message_shown = text;
        ContextId previous = try_close_current_context();
        message_context.open();
        if (text.empty()) {
            message_box->set_display(Display::None);
        }
        else {
            message_label->set_text(text);
            message_box->set_display(Display::Block);
        }
        message_context.close();
        if (previous != ContextId::null()) {
            previous.open();
        }
    }
}

void btga::netplay::show_message(const std::string& text, int seconds) {
    std::lock_guard lock{ message_mutex };
    message_until = seconds > 0 ? std::chrono::steady_clock::now() + std::chrono::seconds(seconds)
                                : std::chrono::steady_clock::time_point::max();
    show_message_now(text);
}

void btga::netplay::message_tick() {
    std::lock_guard lock{ message_mutex };
    if (!message_shown.empty() && std::chrono::steady_clock::now() >= message_until) {
        show_message_now("");
    }
}

// ------------------------------------------- the menu's Netplay tab

namespace {
    const std::string netplay_tab_id = "netplay";
    std::atomic<bool> leave_requested{ false }; // Leave Game: start again on the Netplay screen

    // The menu's Netplay tab (F2) in a session, laid out as the launcher's
    // screen: the players' panel, and the session's beside it.
    void create_netplay_tab_contents(ContextId context, Element* parent) {
        Element* column = context.create_element<Container>(parent, FlexDirection::Column, JustifyContent::FlexStart);
        column->set_padding(24.0f);
        column->set_gap(16.0f);
        column->set_width(100.0f, Unit::Percent);

        Element* cards = context.create_element<Container>(column, FlexDirection::Row, JustifyContent::FlexStart);
        cards->set_gap(20.0f);
        cards->set_width(100.0f, Unit::Percent);
        cards->set_flex_wrap(FlexWrap::Wrap);
        cards->set_align_items(AlignItems::FlexStart);

        Element* players_card = card(context, cards, "Players");
        players_card->set_flex(2.0f, 1.0f, 0.0f);
        players_card->set_min_width(520.0f);
        for (const btga::netplay::Player& player : btga::netplay::players()) {
            // "Bob (host)", then how they are, in smaller text.
            std::string about = player.host ? "host" : "";
            Element* entry = context.create_element<Container>(players_card, FlexDirection::Column, JustifyContent::FlexStart);
            entry->set_gap(2.0f);
            Label* name = context.create_element<Label>(entry, player.name + (about.empty() ? "" : " (" + about + ")"),
                theme::Typography::LabelLG);
            if (player.local) {
                name->set_color(theme::color::Primary);
            }
            std::string status = player.status;
            if (player.ping_ms >= 0) {
                status += ", ping " + std::to_string(player.ping_ms) + " ms";
            }
            if (!player.where.empty()) {
                status += ", " + player.where;
            }
            text_label(context, entry, status, kSmallSize, theme::color::TextDim);
        }

        Element* session_card = card(context, cards, "Session");
        session_card->set_flex(1.0f, 1.0f, 0.0f);
        session_card->set_min_width(360.0f);
        text_label(context, session_card, "Input delay: " + delay_text(btga::netplay::input_delay()), kTextSize);
        if (btga::netplay::settings_in_use()) {
            text_label(context, session_card,
                "Settings for everyone: " + settings_line(btga::netplay::describe_settings(btga::netplay::session_settings_in_use())),
                kSmallSize, theme::color::TextDim);
        }
        text_label(context, session_card, "Chat: T. Enter sends, Escape closes.", kSmallSize, theme::color::TextDim);
        // Out of the session, back to the Netplay screen: the game can't
        // leave a session as it runs, so it starts again.
        Button* leave = context.create_element<Button>(session_card, "Leave Game", ButtonStyle::Danger, ButtonSize::Large);
        leave->add_pressed_callback([] {
            recompui::open_choice_prompt(
                "Leave this game?",
                "The game restarts on the Netplay screen. Anyone still in the game plays on without you.",
                "Leave",
                "Cancel",
                [] {
                    leave_requested = true;
                    ultramodern::quit();
                },
                [] {},
                ButtonStyle::Danger,
                ButtonStyle::Tertiary,
                true);
        });
        text_label(context, session_card, "Cheats, Hacks and Mods come back after the session.",
            kSmallSize, theme::color::TextDim);
    }
}

bool btga::netplay::leaving_to_netplay_screen() {
    return leave_requested.load();
}

void btga::netplay::create_menu_tab() {
    recompui::config::create_tab("Netplay", netplay_tab_id, create_netplay_tab_contents);
    recompui::config::set_tab_visible(netplay_tab_id, false);
}

namespace {
void use_session_menu() {
    // Settings that would change one game and not the other's (the host's
    // are used), and mods (both games must run the same code): hidden for
    // the session.
    for (const char* id : { "cheats", "hacks" }) {
        recompui::config::set_tab_visible(id, false);
    }
    recompui::config::set_tab_visible(recompui::config::mods::id, false);
    recompui::config::set_tab_visible(netplay_tab_id, true);
    // Off a tab just hidden (before the menu's window exists, it opens on
    // the first tab anyway, and this throws).
    try {
        recompui::config::set_tab(recompui::config::general::id);
    }
    catch (const std::exception&) {
    }
}
}
