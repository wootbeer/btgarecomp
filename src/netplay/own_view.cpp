// Netplay: each PC shows only its own player's view of a split-screen
// match, full screen (from the Rage Wars port's own_view.cpp, redone for
// this game's display lists). The display list is changed just before it
// goes to the renderer: only display-list memory (rebuilt every frame, left
// out of the desync check), never the game's state, so every PC's game stays
// the same.
//
// A split-screen frame, as the game builds it (STATUS.md round 201): the
// views are interleaved. For each of its draw buckets, for each view, the main
// list calls that view's viewport list in segment 1 (0x01000150 the top half,
// 0x01000168 the bottom one, others for three and four players; 0x01000138
// is the full screen), loads its projection (G_MTX, projection; one per
// view, 0x40 apart from 0x8012A740) and draws. After the 3D, the second list
// calls the overlay, every player's HUD in screen coordinates under the
// full-screen viewport. The game keeps one player's vertical field of view in
// each view and widens it sideways to the view's shape.
//
// So, for this PC's player: each of the other views' spans (from its
// viewport call to the next viewport call, or the end of its list) is jumped
// over; this view's spans get the full-screen viewport, and its projections a
// copy (in the spare end of the main list's buffer) with the x column scaled
// back to the screen's shape: one player's picture. The other players' HUD
// rectangles are left out, this player's moved to the same place on the full
// screen in widgets (touching rectangles move together), and the line
// between the views goes. Which view is this player's comes from its camera.
// The changed words are put back once the renderer has read the list.
//
// Testing switches: BTGA_SHARED_SCREEN (every view, as the game draws them),
// BTGA_OWNVIEW_DUMP=<frame> and BTGA_OWNVIEW_RECTS=<frame> (a frame's
// display list, or its rectangles after the changes, to the log).
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "recomp.h"
#include "recompui/renderer.h"
#include "ultramodern/renderer_context.hpp"

#include "btga_netplay.h"
#include "session.h"

namespace {
    uint8_t* own_rdram = nullptr;

    struct Command {
        uint32_t address;
        uint32_t w0, w1;
        int depth;
    };

    uint32_t k0(uint32_t address) {
        return (address & 0x1FFFFFFFu) | 0x80000000u;
    }

    uint32_t word(uint8_t* rdram, uint32_t address) {
        return (uint32_t)MEM_W(0, (gpr)(int32_t)k0(address));
    }

    constexpr int kMaxCommands = 200000;

    // The frame's commands in the order the renderer meets them (F3DEX2).
    std::vector<Command> walk(uint8_t* rdram, uint32_t root) {
        std::vector<Command> commands;
        std::vector<uint32_t> stack;
        uint32_t segments[16] = {};
        uint32_t address = k0(root);
        for (int n = 0; n < kMaxCommands; n++) {
            uint32_t w0 = word(rdram, address), w1 = word(rdram, address + 4);
            uint32_t op = w0 >> 24;
            commands.push_back({ address, w0, w1, (int)stack.size() });
            if (op == 0xDB && ((w0 >> 16) & 0xFF) == 6) { // G_MOVEWORD, a segment
                segments[(w0 & 0xFFFF) / 4 & 15] = w1;
            }
            if (op == 0xDE) { // G_DL
                uint32_t target = w1;
                if ((target >> 24) < 16 && target < 0x80000000u) {
                    target = segments[target >> 24] + (target & 0xFFFFFF);
                }
                if (((w0 >> 16) & 0xFF) == 0) {
                    if (stack.size() >= 18) {
                        return {};
                    }
                    stack.push_back(address + 8);
                }
                address = k0(target);
                continue;
            }
            if (op == 0xDF) { // G_ENDDL
                if (stack.empty()) {
                    return commands;
                }
                address = stack.back();
                stack.pop_back();
                continue;
            }
            address += 8;
        }
        return {};
    }

    // BTGA_OWNVIEW_DUMP=<frame>: that frame's display list, the commands that
    // matter here, to the log (studying the game's split screen).
    void dump(uint8_t* rdram, uint32_t root) {
        static int frame = 0;
        static const char* at = std::getenv("BTGA_OWNVIEW_DUMP");
        if (at == nullptr || frame++ != std::atoi(at)) {
            return;
        }
        std::vector<Command> commands = walk(rdram, root);
        std::fprintf(stderr, "[OWNVIEW] root %08X, %zu commands\n", root, commands.size());
        for (const Command& c : commands) {
            uint32_t op = c.w0 >> 24;
            bool show = op == 0xDA || op == 0xDC || op == 0xDE || op == 0xDF || op == 0xED || op == 0xDB || op == 0xD9 ||
                op == 0xE4 || op == 0xE5 || op == 0xF6 || op == 0x64;
            if (!show) {
                continue;
            }
            std::string extra;
            if (op == 0xDC && (c.w0 & 0xFF) == 8) {
                uint32_t vp = k0(c.w1);
                char buf[96];
                std::snprintf(buf, sizeof(buf), " vp scale %d,%d trans %d,%d", (int16_t)MEM_H(0, (gpr)(int32_t)vp) / 4,
                    (int16_t)MEM_H(2, (gpr)(int32_t)vp) / 4, (int16_t)MEM_H(8, (gpr)(int32_t)vp) / 4, (int16_t)MEM_H(10, (gpr)(int32_t)vp) / 4);
                extra = buf;
            }
            std::fprintf(stderr, "[OWNVIEW] %08X d%d %08X %08X%s\n", c.address, c.depth, c.w0, c.w1, extra.c_str());
        }
        std::fflush(stderr);
    }

    constexpr uint32_t kFullScreenViewportDl = 0x01000138;
    constexpr uint32_t kCameras = 0x80235F00; // the players' cameras, in player order
    constexpr uint32_t kCameraStride = 0x250;
    constexpr uint32_t kCameraViewportDl = 0x118;
    // The game's state (func_800A8690 sets 5 when Start pauses the match):
    // 7 for the match's start (the intro and countdown, about 900 frames in
    // two-player Tank Wars), then 3 while it plays, 5 paused.
    constexpr uint32_t kPlayState = 0x802195C8;
    bool match_playing(uint32_t state) {
        return state == 3 || state == 7;
    }
    constexpr uint32_t kMainListBuffers = 0x80C40000; // two, src/game/draw_limits.cpp
    constexpr uint32_t kMainListBufferSize = 0x40000;
    constexpr uint32_t kScratchOffset = 0x3F000;      // past the cut-off (0x30000) and its slack
    constexpr uint32_t kScratchSize = 0x1000;

    bool is_viewport_call(const Command& c) {
        return (c.w0 >> 24) == 0xDE && ((c.w0 >> 16) & 0xFF) == 0 && (c.w1 >> 24) == 0x01 && (c.w1 & 0xFFFFFF) >= 0x80 &&
            (c.w1 & 0xFFFFFF) < 0x400;
    }

    struct Rect {
        float x0, y0, w, h;
        bool contains(float x, float y) const { return x >= x0 - 0.5f && x <= x0 + w + 0.5f && y >= y0 - 0.5f && y <= y0 + h + 0.5f; }
    };

    // A viewport list's rectangle: its G_MOVEMEM viewport (segment-relative).
    bool viewport_rect(uint8_t* rdram, const std::vector<Command>& commands, size_t call, uint32_t segment1, Rect& rect) {
        for (size_t i = call + 1; i < commands.size() && commands[i].depth > commands[call].depth; i++) {
            const Command& c = commands[i];
            if ((c.w0 >> 24) == 0xDC && (c.w0 & 0xFF) == 8) {
                uint32_t vp = c.w1;
                if (((vp >> 24) & 0xF) == 1) {
                    vp = segment1 + (vp & 0xFFFFFF);
                }
                vp = k0(vp);
                const float sx = (int16_t)MEM_H(0, (gpr)(int32_t)vp) / 4.0f, sy = (int16_t)MEM_H(2, (gpr)(int32_t)vp) / 4.0f;
                const float tx = (int16_t)MEM_H(8, (gpr)(int32_t)vp) / 4.0f, ty = (int16_t)MEM_H(10, (gpr)(int32_t)vp) / 4.0f;
                if (sx <= 0.0f || sy <= 0.0f) {
                    return false;
                }
                rect = { tx - sx, ty - sy, 2.0f * sx, 2.0f * sy };
                return true;
            }
        }
        return false;
    }

    int16_t half_word(uint8_t* rdram, uint32_t address) {
        return MEM_H(0, (gpr)(int32_t)address);
    }

    int32_t fixed_at(uint8_t* rdram, uint32_t m, int i) {
        return (int32_t)(((uint32_t)(uint16_t)half_word(rdram, m + i * 2) << 16) | (uint16_t)half_word(rdram, m + 32 + i * 2));
    }

    // A projection's copy, its x column scaled.
    void scale_projection_x(uint8_t* rdram, uint32_t from, uint32_t to, double kx) {
        for (int i = 0; i < 16; i++) {
            int32_t value = fixed_at(rdram, from, i);
            if (i % 4 == 0) {
                value = (int32_t)std::lround((double)value * kx);
            }
            MEM_H(0, (gpr)(int32_t)(to + i * 2)) = (int16_t)(value >> 16);
            MEM_H(0, (gpr)(int32_t)(to + 32 + i * 2)) = (int16_t)(value & 0xFFFF);
        }
    }

    // The words changed for this frame, as they were: put back once the
    // renderer has read the display list (it reads it all in send_dl), so
    // the game's memory is never left changed (the HUD's lists are in it).
    std::vector<std::pair<uint32_t, uint64_t>> changed;

    void set_command(uint8_t* rdram, uint32_t address, uint32_t w0, uint32_t w1) {
        const uint64_t was = ((uint64_t)(uint32_t)MEM_W(0, (gpr)(int32_t)address) << 32) | (uint32_t)MEM_W(4, (gpr)(int32_t)address);
        changed.emplace_back(address, was);
        MEM_W(0, (gpr)(int32_t)address) = (int32_t)w0;
        MEM_W(4, (gpr)(int32_t)address) = (int32_t)w1;
    }

    // A display list (in the scratch area) that calls viewport list `dl`, then
    // sets the scissor to a line far below the screen.
    constexpr uint32_t kHiddenDls = 0x100; // at the scratch area's end, 0x20 a view
    uint32_t hidden_view_dl(uint8_t* rdram, uint32_t buffer, size_t view, uint32_t dl) {
        const uint32_t at = buffer + kScratchOffset + kScratchSize - kHiddenDls + (uint32_t)(view % 8) * 0x20;
        set_command(rdram, at, 0xDE000000u, dl);                                // G_DL, call
        set_command(rdram, at + 8, 0xED000000u | (0 << 12) | (1000 * 4), (320u * 4 << 12) | (1001 * 4)); // G_SETSCISSOR
        set_command(rdram, at + 16, 0xDF000000u, 0);                           // G_ENDDL
        return at;
    }

    // Frames that look split but are left as they are, for the log.
    int unhandled_logged = 0;

    void own_view(uint8_t* rdram, uint32_t root, int player) {
        std::vector<Command> commands = walk(rdram, root);
        if (commands.empty()) {
            return;
        }
        // Segment 1 (the viewport lists), and the main list's buffer.
        uint32_t segment1 = 0, buffer = 0;
        for (const Command& c : commands) {
            if ((c.w0 >> 24) == 0xDB && ((c.w0 >> 16) & 0xFF) == 6 && (c.w0 & 0xFFFF) == 4) {
                segment1 = c.w1;
            }
            if (buffer == 0 && c.address >= kMainListBuffers && c.address < kMainListBuffers + 2 * kMainListBufferSize) {
                buffer = kMainListBuffers + (c.address - kMainListBuffers) / kMainListBufferSize * kMainListBufferSize;
            }
        }
        // The views: viewport lists other than the full screen's, in the order
        // they're first drawn (the players').
        std::vector<uint32_t> view_dls;
        std::vector<Rect> view_rects;
        Rect full{};
        bool have_full = false;
        for (size_t i = 0; i < commands.size(); i++) {
            const Command& c = commands[i];
            if (!is_viewport_call(c)) {
                continue;
            }
            if (c.w1 == kFullScreenViewportDl) {
                have_full = have_full || viewport_rect(rdram, commands, i, segment1, full);
                continue;
            }
            if (std::find(view_dls.begin(), view_dls.end(), c.w1) == view_dls.end()) {
                Rect r{};
                if (!viewport_rect(rdram, commands, i, segment1, r)) {
                    continue; // a segment 1 list that sets no viewport (the frame's setup)
                }
                view_dls.push_back(c.w1);
                view_rects.push_back(r);
            }
        }
        if (view_dls.size() < 2 || !have_full || buffer == 0) {
            if (view_dls.size() >= 2 && unhandled_logged++ < 8) {
                std::fprintf(stderr, "[NET] own view: %zu views, but no full-screen viewport or main list buffer\n", view_dls.size());
            }
            return;
        }
        if (player < 0 || player >= (int)view_dls.size()) {
            return;
        }
        // This player's view: its camera's viewport list (the players'
        // cameras, kCameraStride apart from kCameras; +0x118 the list, as
        // src/game/widescreen.cpp reads it), else the order the views came in.
        size_t mine_index = (size_t)player;
        const uint32_t camera_dl = (uint32_t)MEM_W(0, (gpr)(int32_t)(kCameras + kCameraStride * (uint32_t)player + kCameraViewportDl));
        auto found_view = std::find(view_dls.begin(), view_dls.end(), camera_dl);
        if (found_view != view_dls.end()) {
            mine_index = (size_t)(found_view - view_dls.begin());
        }
        const Rect mine = view_rects[mine_index];
        const uint32_t mine_dl = view_dls[mine_index];
        // The x column scaled from the view's shape back to the screen's.
        const double kx = ((double)mine.w / mine.h) / ((double)full.w / full.h);

        // The views' spans: from a viewport call to the next viewport call or
        // the end of its list, at its depth.
        std::vector<bool> in_span(commands.size(), false);
        std::unordered_map<uint32_t, uint32_t> copies; // projection -> its copy
        uint32_t next_copy = buffer + kScratchOffset;
        auto delimits = [&](const Command& e) {
            return is_viewport_call(e) && (e.w1 == kFullScreenViewportDl || std::find(view_dls.begin(), view_dls.end(), e.w1) != view_dls.end());
        };
        for (size_t i = 0; i < commands.size(); i++) {
            const Command& c = commands[i];
            if (!delimits(c) || c.w1 == kFullScreenViewportDl) {
                continue;
            }
            size_t end = i + 1;
            while (end < commands.size()) {
                const Command& e = commands[end];
                if (e.depth < c.depth || (e.depth == c.depth && (delimits(e) || (e.w0 >> 24) == 0xDF))) {
                    break;
                }
                end++;
            }
            if (end >= commands.size() || commands[end].depth != c.depth) {
                return; // not as expected: left as it is
            }
            std::fill(in_span.begin() + (std::ptrdiff_t)i, in_span.begin() + (std::ptrdiff_t)end, true);
            if (c.w1 != mine_dl) {
                // Drawn nowhere: its viewport list, then a scissor below the
                // screen. Not jumped over: the game sets the textures and
                // modes an object needs only when they differ from the last
                // object's, so the views after it need what it sets.
                const size_t view = (size_t)(std::find(view_dls.begin(), view_dls.end(), c.w1) - view_dls.begin());
                set_command(rdram, c.address, c.w0, hidden_view_dl(rdram, buffer, view, c.w1));
                continue;
            }
            set_command(rdram, c.address, c.w0, kFullScreenViewportDl);
            for (size_t k = i + 1; k < end; k++) {
                const Command& m = commands[k];
                // Fill rectangles drawn in the view (a fade over it, the
                // black between places in a tunnel): over the whole screen,
                // as the 3D is.
                if ((m.w0 >> 24) == 0xF6) {
                    auto to_x = [&](uint32_t v) { return full.x0 + ((float)v / 4.0f - mine.x0) * full.w / mine.w; };
                    auto to_y = [&](uint32_t v) { return full.y0 + ((float)v / 4.0f - mine.y0) * full.h / mine.h; };
                    auto fixed = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 4.0f), 0, 0xFFF); };
                    const uint32_t w0 = 0xF6000000u | (fixed(to_x((m.w0 >> 12) & 0xFFF)) << 12) | fixed(to_y(m.w0 & 0xFFF));
                    const uint32_t w1 = (fixed(to_x((m.w1 >> 12) & 0xFFF)) << 12) | fixed(to_y(m.w1 & 0xFFF));
                    set_command(rdram, m.address, w0, w1);
                    continue;
                }
                if ((m.w0 >> 24) != 0xDA || (m.w0 & 4) == 0) {
                    continue;
                }
                auto found = copies.find(m.w1);
                if (found == copies.end()) {
                    if (next_copy + 64 > buffer + kScratchOffset + kScratchSize - kHiddenDls) {
                        continue;
                    }
                    uint32_t from = m.w1;
                    if ((from >> 24) == 0) {
                        from |= 0x80000000u;
                    }
                    scale_projection_x(rdram, k0(from), next_copy, kx);
                    found = copies.emplace(m.w1, next_copy).first;
                    next_copy += 64;
                }
                set_command(rdram, m.address, m.w0, found->second);
            }
        }

        // The HUD: rectangles outside the views' spans (the overlay). Each is
        // the view's its middle lies in (in two-player the radars, stacked on
        // the line between the views, reach a little into the other view).
        // Another player's are left out; this player's move, in four groups
        // (the view's top or bottom half, left or right), each as one block to
        // the same edges of the screen, so the radar's box and dots stay
        // together; a group that would leave the screen is moved back onto it.
        // Only while the match plays: paused (and between screens) the game's
        // menus are drawn over the whole screen, and stay as they are.
        if (!match_playing((uint32_t)MEM_W(0, (gpr)(int32_t)kPlayState))) {
            return;
        }
        struct HudRect {
            size_t index;
            float left, top, right, bottom;
            int group;
            int align; // the index of the rect-align command it's under, -1 none
            bool bar;  // the health bar's (wide and thin)
            int view;  // the view it's in
            bool dropped = false; // another player's
        };
        std::vector<HudRect> hud;
        // In widescreen the HUD's rectangles are anchored to an edge by
        // RT64 rect-align commands (gEXSetRectAlign, src/game/widescreen.cpp),
        // chosen by each element's place in the split layout: a box and its
        // icon could be anchored differently and part. Each command's rects,
        // to give it the anchoring of where they end up (below); one that
        // also anchors something else is left as it is.
        int align_at = -1;
        std::unordered_map<int, bool> align_shared; // command index -> anchors other rects too
        for (size_t n = 0; n < commands.size(); n++) {
            const Command& c = commands[n];
            const uint32_t op = c.w0 >> 24;
            if (c.w0 == 0x64000006u && n + 1 < commands.size()) {
                align_at = (int)n;
                continue;
            }
            if (in_span[n] || (op != 0xE4 && op != 0xE5 && op != 0xF6)) {
                continue;
            }
            const float left = (float)((c.w1 >> 12) & 0xFFF) / 4.0f, top = (float)(c.w1 & 0xFFF) / 4.0f;
            const float right = (float)((c.w0 >> 12) & 0xFFF) / 4.0f, bottom = (float)(c.w0 & 0xFFF) / 4.0f;
            const float w = right - left, h = bottom - top;
            const float cx = (left + right) / 2.0f, cy = (top + bottom) / 2.0f;
            // The line between the views: thin and across (or down) the screen.
            if (op == 0xF6 && ((w >= full.w * 0.9f && h <= 6.0f) || (h >= full.h * 0.9f && w <= 6.0f))) {
                set_command(rdram, c.address, 0, 0); // G_SPNOOP
                continue;
            }
            int within = -1;
            for (size_t v = 0; v < view_rects.size() && within < 0; v++) {
                const Rect& r = view_rects[v];
                if (r.contains(cx, cy) && w <= r.w * 1.05f && h <= r.h * 1.4f) {
                    within = (int)v;
                }
            }
            if (within < 0) {
                if (align_at >= 0) {
                    align_shared[align_at] = true;
                }
                continue; // over the whole screen: as it is
            }
            hud.push_back({ n, left, top, right, bottom, -1, align_at, w >= 40.0f && h <= 10.0f, within });
        }
        // The health bar's fill is as wide as the health left: anything
        // inside the bar's frame is the bar's too.
        for (HudRect& r : hud) {
            if (r.bar) {
                continue;
            }
            for (const HudRect& frame : hud) {
                if (frame.bar && r.left >= frame.left - 2.0f && r.right <= frame.right + 2.0f && r.top >= frame.top - 2.0f &&
                    r.bottom <= frame.bottom + 2.0f) {
                    r.bar = true;
                    break;
                }
            }
        }
        // Widgets: rectangles that touch or nearly touch (a box and its icon,
        // the radar and its dots, a line of text) move together.
        std::vector<int> parent(hud.size());
        for (size_t i = 0; i < hud.size(); i++) {
            parent[i] = (int)i;
        }
        auto root_of = [&](int i) {
            while (parent[i] != i) {
                parent[i] = parent[parent[i]];
                i = parent[i];
            }
            return i;
        };
        // Within a view, rectangles up to kGap apart (the letters of a word,
        // a box and its icon); between views only touching ones (the radar's
        // two strips), not a message a few pixels from it.
        constexpr float kGap = 3.0f;
        constexpr float kTouch = 0.5f;
        for (size_t i = 0; i < hud.size(); i++) {
            for (size_t j = i + 1; j < hud.size(); j++) {
                const HudRect& p = hud[i];
                const HudRect& q = hud[j];
                if (p.bar != q.bar) {
                    continue; // the health bar moves on its own (below)
                }
                const float gap = p.view == q.view ? kGap : kTouch;
                if (p.left <= q.right + gap && q.left <= p.right + gap && p.top <= q.bottom + gap && q.top <= p.bottom + gap) {
                    parent[root_of((int)i)] = root_of((int)j);
                }
            }
        }
        // Another player's are left out, unless joined to this player's: the
        // radar in two-player is one for both, drawn half by each player's
        // HUD (its map in two strips) across the line between the views.
        std::unordered_map<int, bool> has_mine;
        for (size_t i = 0; i < hud.size(); i++) {
            if (hud[i].view == (int)mine_index) {
                has_mine[root_of((int)i)] = true;
            }
        }
        for (size_t i = 0; i < hud.size(); i++) {
            if (has_mine.count(root_of((int)i)) == 0) {
                hud[i].dropped = true;
                set_command(rdram, commands[hud[i].index].address, 0, 0); // G_SPNOOP
            }
        }
        struct Widget {
            float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
            float dx = 0.0f, dy = 0.0f;
            bool bar = false;
            bool shared = false; // several players' (the two-player radar)
        };
        std::unordered_map<int, Widget> widgets;
        for (size_t i = 0; i < hud.size(); i++) {
            if (hud[i].dropped) {
                continue;
            }
            Widget& w = widgets[root_of((int)i)];
            w.left = std::min(w.left, hud[i].left);
            w.top = std::min(w.top, hud[i].top);
            w.right = std::max(w.right, hud[i].right);
            w.bottom = std::max(w.bottom, hud[i].bottom);
            w.bar = w.bar || hud[i].bar;
            w.shared = w.shared || hud[i].view != (int)mine_index;
            hud[i].group = root_of((int)i);
        }
        // Each to the same edges of the screen as it's near in the view (by
        // its middle), kept on the screen.
        for (auto& [id, w] : widgets) {
            const float cx = (w.left + w.right) / 2.0f, cy = (w.top + w.bottom) / 2.0f;
            w.dx = cx >= mine.x0 + mine.w / 2.0f ? (full.x0 + full.w) - (mine.x0 + mine.w) : full.x0 - mine.x0;
            // A shared widget (the two-player radar, on the line between
            // the views) to the bottom for everyone, as with one player.
            w.dy = w.shared || cy >= mine.y0 + mine.h / 2.0f ? (full.y0 + full.h) - (mine.y0 + mine.h) : full.y0 - mine.y0;
            if (w.right + w.dx > full.x0 + full.w) w.dx -= w.right + w.dx - (full.x0 + full.w);
            if (w.left + w.dx < full.x0) w.dx += full.x0 - (w.left + w.dx);
            // Up and down, at least the game's own TV-safe margin from the
            // screen's edge (a TV may crop it): a widget on the line between
            // the views (the radar) would otherwise touch it.
            constexpr float kSafe = 16.0f;
            if (w.bottom + w.dy > full.y0 + full.h - kSafe) w.dy -= w.bottom + w.dy - (full.y0 + full.h - kSafe);
            if (w.top + w.dy < full.y0 + kSafe) w.dy += full.y0 + kSafe - (w.top + w.dy);
            // The shared radar: at the bottom on every PC, as with one player.
            if (w.shared) {
                w.dy = full.y0 + full.h - kSafe - w.bottom;
            }
            // The health bar: centred at the bottom, as with one player.
            if (w.bar) {
                w.dx = full.x0 + full.w / 2.0f - (w.left + w.right) / 2.0f;
                w.dy = full.y0 + full.h - kSafe - w.bottom;
            }
        }
        // Each rect-align command: the anchoring of where its rects end up
        // (as src/game/widescreen.cpp picks it by x: left of 120 the left
        // edge, from 200 the right, between them and the bar none), if they
        // agree. With no rect-align commands (Original, three or four
        // players) nothing is anchored and this does nothing.
        // A command that also anchors other rects (text over the screen)
        // stays as it is, and the pieces under it decide their widget's
        // anchoring: the rest follow them, so the widget stays together.
        auto side_of = [&](int at) {
            const uint32_t origin = commands[(size_t)at].w1 & 0xFFF;
            return origin == 0x000 ? 0 : origin == 0x800 ? 1 : origin == 0x400 ? 2 : -1;
        };
        std::unordered_map<int, int> widget_side; // widget -> 0 left, 1 none, 2 right, -1 leave alone
        for (const HudRect& r : hud) {
            if (r.dropped || r.align < 0 || align_shared.count(r.align) == 0) {
                continue;
            }
            const int fixed = side_of(r.align);
            auto [it, added] = widget_side.emplace(r.group, fixed);
            if (!added && it->second != fixed) {
                it->second = -1;
            }
        }
        std::unordered_map<int, int> align_side; // command index -> 0 left, 1 none, 2 right, -1 mixed
        for (const HudRect& r : hud) {
            if (r.dropped || r.align < 0) {
                continue;
            }
            const Widget& w = widgets[r.group];
            const float cx = (w.left + w.right) / 2.0f + w.dx;
            int side = w.bar ? 1 : cx < 120.0f ? 0 : cx >= 200.0f ? 2 : 1;
            if (auto fixed = widget_side.find(r.group); fixed != widget_side.end()) {
                side = fixed->second;
            }
            auto [it, added] = align_side.emplace(r.align, side);
            if (!added && it->second != side) {
                it->second = -1;
            }
        }
        for (const auto& [at, side] : align_side) {
            if (side < 0 || align_shared.count(at) != 0) {
                continue;
            }
            const Command& a = commands[(size_t)at];
            const Command& offsets = commands[(size_t)at + 1];
            // As widescreen.cpp's kAlignHudLeft, kAlignNone, kAlignHudRight
            // (origins, then offsets in 10.2 fixed point); already set: as is.
            constexpr uint32_t kOrigin[3] = { 0x000, 0x800, 0x400 };
            constexpr int16_t kOffset[3] = { -16 * 4, 0, -320 * 4 + 16 * 4 };
            const uint32_t w1 = kOrigin[side] | (kOrigin[side] << 12);
            const uint32_t off = (uint32_t)(uint16_t)kOffset[side] << 16;
            if (a.w1 == w1 && offsets.w0 == off && offsets.w1 == off) {
                continue;
            }
            set_command(rdram, a.address, a.w0, w1);
            set_command(rdram, offsets.address, off, off);
        }
        auto clamp12 = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 4.0f), 0, 0xFFF); };
        for (const HudRect& r : hud) {
            if (r.dropped) {
                continue;
            }
            const Command& c = commands[r.index];
            const float mx = widgets[r.group].dx, my = widgets[r.group].dy;
            const uint32_t w0 = (c.w0 & 0xFF000000u) | (clamp12(r.right + mx) << 12) | clamp12(r.bottom + my);
            const uint32_t w1 = (c.w1 & 0xFF000000u) | (clamp12(r.left + mx) << 12) | clamp12(r.top + my);
            set_command(rdram, c.address, w0, w1);
        }
    }

    // BTGA_OWNVIEW_RECTS=<frame>: that frame's rectangles and scissors after
    // the own-view changes, decoded, with the viewport list they're under.
    void dump_rects(uint8_t* rdram, uint32_t root) {
        static int frame = 0;
        static const char* at = std::getenv("BTGA_OWNVIEW_RECTS");
        if (at == nullptr || frame++ != std::atoi(at)) {
            return;
        }
        std::vector<Command> commands = walk(rdram, root);
        uint32_t under = 0;
        for (const Command& c : commands) {
            const uint32_t op = c.w0 >> 24;
            if (op == 0xDE && (c.w1 >> 24) == 0x01) {
                under = c.w1;
            }
            if (op == 0xDE && c.w1 >= 0x80C40000u && c.w1 < 0x80D00000u) {
                under = c.w1; // ours (hidden)
            }
            if (op == 0xE4 || op == 0xE5 || op == 0xF6 || op == 0xED) {
                const float a = ((c.w1 >> 12) & 0xFFF) / 4.0f, b = (c.w1 & 0xFFF) / 4.0f;
                const float x = ((c.w0 >> 12) & 0xFFF) / 4.0f, y = (c.w0 & 0xFFF) / 4.0f;
                if (op == 0xED) {
                    std::fprintf(stderr, "[OVRECT] %08X d%d ED scissor %g,%g-%g,%g under %08X\n", c.address, c.depth, x, y, a, b, under);
                }
                else {
                    std::fprintf(stderr, "[OVRECT] %08X d%d %02X %g,%g-%g,%g under %08X\n", c.address, c.depth, op, a, b, x, y, under);
                }
            }
        }
        std::fflush(stderr);
    }

    // The renderer, each display list seen first.
    class OwnViewContext : public ultramodern::renderer::RendererContext {
    public:
        explicit OwnViewContext(std::unique_ptr<ultramodern::renderer::RendererContext> inner) : inner(std::move(inner)) {}
        bool valid() override { return inner->valid(); }
        ultramodern::renderer::SetupResult get_setup_result() const override { return inner->get_setup_result(); }
        ultramodern::renderer::GraphicsApi get_chosen_api() const override { return inner->get_chosen_api(); }
        bool update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) override {
            return inner->update_config(old_config, new_config);
        }
        void enable_instant_present() override { inner->enable_instant_present(); }
        void send_dl(const OSTask* task) override {
            if (own_rdram != nullptr) {
                dump(own_rdram, (uint32_t)task->t.data_ptr);
                // In a session: only this PC's player's view, full screen.
                if (btga::netplay::active() && std::getenv("BTGA_SHARED_SCREEN") == nullptr) {
                    own_view(own_rdram, (uint32_t)task->t.data_ptr, btga::netplay::local_port());
                }
                dump_rects(own_rdram, (uint32_t)task->t.data_ptr);
            }
            inner->send_dl(task);
            // Back as the game left them, newest change first.
            uint8_t* rdram = own_rdram;
            for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
                MEM_W(0, (gpr)(int32_t)it->first) = (int32_t)(uint32_t)(it->second >> 32);
                MEM_W(4, (gpr)(int32_t)it->first) = (int32_t)(uint32_t)it->second;
            }
            changed.clear();
        }
        void send_dummy_workload(uint32_t fb_address) override { inner->send_dummy_workload(fb_address); }
        void update_screen() override {
            // Netplay waiting for another player: the last frame again, with
            // the message over it (the game sends none of its own meanwhile).
            auto* rt64 = btga::netplay::screen_held() ? dynamic_cast<recompui::renderer::RT64Context*>(inner.get()) : nullptr;
            if (rt64 != nullptr) {
                rt64->update_screen_held();
            }
            else {
                inner->update_screen();
            }
        }
        void shutdown() override { inner->shutdown(); }
        uint32_t get_display_framerate() const override { return inner->get_display_framerate(); }
        float get_resolution_scale() const override { return inner->get_resolution_scale(); }
    private:
        std::unique_ptr<ultramodern::renderer::RendererContext> inner;
    };
}

std::unique_ptr<ultramodern::renderer::RendererContext> btga::netplay::wrap_renderer(
    uint8_t* rdram, std::unique_ptr<ultramodern::renderer::RendererContext> inner) {
    own_rdram = rdram;
    return std::make_unique<OwnViewContext>(std::move(inner));
}
