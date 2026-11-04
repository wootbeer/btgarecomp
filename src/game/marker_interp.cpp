// Smooth radar arrows at higher refresh rates (round 153).
//
// The radar pickup's arrows over the other tanks are texture rectangles that
// func_800C8484 draws at the tanks' projected screen positions (round 152).
// RT64 interpolates 3D and orthographic draws between game frames by pairing
// their matrices, but never rectangles, so above 30 fps the tanks moved
// smoothly and their arrows stepped along at 30.
//
// So each arrow's texture rectangle is replaced by a call to a small display
// list that draws the same texels as two triangles under an orthographic
// projection, moved into place by a modelview matrix in a matrix group with
// its own ID. RT64 then interpolates that matrix like any object's. An arrow
// keeps its ID from frame to frame by pairing it with the nearest arrow of
// the same widget call in the previous frame; one with no partner nearby
// gets a new ID and is drawn where it is.
//
// The widget's commands are only rewritten once it has returned: the
// rectangle (G_TEXRECT plus its two RDPHALF words) becomes a G_DL call and
// two no-ops, so nothing moves in the game's display list. The called lists,
// matrices and vertices go in the last 128 KB of RDRAM (kept out of the heap
// by heap_relocate.cpp), in rotating slots so a list isn't overwritten while
// RT64 may still read it.
#include <cmath>
#include <cstdint>
#include <vector>

#include "recomp.h"

#include "btga_markers.h"

namespace {
    constexpr uint32_t kOverlayDlHeadPtr = 0x803A5944; // func_800BC9F4's display-list head

    constexpr uint32_t kScratchStart = btga::markers::kScratchStart;
    constexpr uint32_t kSlotSize = 0x4000;
    constexpr uint32_t kSlotCount = btga::markers::kScratchSize / kSlotSize;

    constexpr uint32_t kIdBase = 0x4D4B0000; // 'MK', clear of the RDRAM addresses interpolation_ids.cpp uses
    constexpr int kPairDistance = 24;        // pixels an arrow may move between frames and keep its ID
    constexpr size_t kMaxCalls = 8;          // widget calls per frame (one per player view)

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    struct Marker {
        int x, y; // upper left, pixels
        uint32_t id;
    };

    std::vector<Marker> prev_frame[kMaxCalls], cur_frame[kMaxCalls];
    size_t call_index = 0;
    uint32_t next_id = 0;
    uint32_t slot = 0;

    bool in_marker_widget = false;
    uint32_t widget_start = 0;

    uint32_t take_id(int call, int x, int y) {
        if (call < (int)kMaxCalls) {
            std::vector<Marker>& prev = prev_frame[call];
            int best = -1, best_dist = kPairDistance * kPairDistance + 1;
            for (size_t i = 0; i < prev.size(); i++) {
                int dx = prev[i].x - x, dy = prev[i].y - y;
                int d = dx * dx + dy * dy;
                if (d < best_dist) {
                    best = (int)i;
                    best_dist = d;
                }
            }
            if (best >= 0) {
                uint32_t id = prev[best].id;
                prev.erase(prev.begin() + best);
                return id;
            }
        }
        next_id = (next_id + 1) & 0xFFFF;
        return kIdBase | next_id;
    }

    struct Writer {
        uint8_t* rdram;
        uint32_t addr, end;
        bool ok() const { return addr <= end; }
        void w(uint32_t value) {
            if (addr + 4 <= end) {
                MEM_W(0, kseg0(addr)) = (int32_t)value;
            }
            addr += 4;
        }
        void cmd(uint32_t w0, uint32_t w1) { w(w0); w(w1); }
        void h(uint16_t value) {
            if (addr + 2 <= end) {
                MEM_H(0, kseg0(addr)) = (int16_t)value;
            }
            addr += 2;
        }
        void b(uint8_t value) {
            if (addr + 1 <= end) {
                MEM_B(0, kseg0(addr)) = (int8_t)value;
            }
            addr += 1;
        }
    };

    // An N64 Mtx (s15.16, integer halves then fractions) from a row-major float matrix.
    void write_mtx(Writer& out, const float m[4][4]) {
        int32_t fixed[16];
        for (int i = 0; i < 16; i++) {
            fixed[i] = (int32_t)std::lround(m[i / 4][i % 4] * 65536.0f);
        }
        for (int i = 0; i < 16; i++) {
            out.h((uint16_t)((uint32_t)fixed[i] >> 16));
        }
        for (int i = 0; i < 16; i++) {
            out.h((uint16_t)((uint32_t)fixed[i] & 0xFFFF));
        }
    }

    // Vertex coordinates are in quarter pixels (the rectangle's 10.2 units),
    // so the modelview scales by 1/4 on top of the projection's own factors.
    // The projection maps x 0..256 to -1..1 and y 0..256 to 1..-1 (powers of
    // two, exact in s15.16); the modelview brings 320 x 240 to that.
    constexpr float kScaleX = 256.0f / 320.0f / 4.0f;
    constexpr float kScaleY = 256.0f / 240.0f / 4.0f;

    // Replaces the rectangle at `rect` (G_TEXRECT, G_RDPHALF_1, G_RDPHALF_2)
    // with a call to a list drawn at `out`. False if it doesn't fit.
    bool convert(uint8_t* rdram, uint32_t rect, Writer& out, int call) {
        uint32_t w0 = (uint32_t)MEM_W(0, kseg0(rect));
        uint32_t w1 = (uint32_t)MEM_W(4, kseg0(rect));
        uint32_t st = (uint32_t)MEM_W(12, kseg0(rect));
        uint32_t dsdt = (uint32_t)MEM_W(20, kseg0(rect));
        int lrx = (w0 >> 12) & 0xFFF, lry = w0 & 0xFFF;
        int tile = (w1 >> 24) & 7;
        int ulx = (w1 >> 12) & 0xFFF, uly = w1 & 0xFFF;
        int16_t s = (int16_t)(st >> 16), t = (int16_t)(st & 0xFFFF);
        int16_t dsdx = (int16_t)(dsdt >> 16), dtdy = (int16_t)(dsdt & 0xFFFF);
        int w = lrx - ulx, h = lry - uly;
        if (w <= 0 || h <= 0) {
            return false;
        }
        // Texture coordinates (s10.5) at the corners. A rectangle samples
        // each pixel at s + i * dsdx, a triangle at the pixel's centre, so
        // the quad starts half a pixel's step earlier (dsdx is s5.10 per
        // pixel; w is in quarter pixels).
        int16_t s0 = (int16_t)(s - dsdx / 64), t0 = (int16_t)(t - dtdy / 64);
        int16_t s1 = (int16_t)(s0 + (w * dsdx) / 128);
        int16_t t1 = (int16_t)(t0 + (h * dtdy) / 128);

        uint32_t id = take_id(call, ulx / 4, uly / 4);
        if (call < (int)kMaxCalls) {
            cur_frame[call].push_back({ ulx / 4, uly / 4, id });
        }

        const uint32_t start = out.addr;
        constexpr uint32_t kListSize = 26 * 8;
        const uint32_t viewport = start + kListSize;
        const uint32_t proj = viewport + 16;
        const uint32_t model = proj + 64;
        const uint32_t vtx = model + 64;
        const uint32_t total = vtx + 4 * 16 - start;
        if (start + total > out.end) {
            return false;
        }

        auto phys = [](uint32_t a) { return a & 0x1FFFFFFF; };
        out.cmd(0xE0525464, 0x10000064);               // gEXEnable
        out.cmd(0x64000015, 0);                        // gEXPushViewport
        out.cmd(0xDC080008, phys(viewport));           // gSPViewport (full screen)
        out.cmd(0x6400001D, 0);                        // gEXPushProjectionMatrix
        out.cmd(0xDA380007, phys(proj));               // gSPMatrix(G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH)
        out.cmd(0x64000029, 0);                        // gEXPushGeometryMode
        // gSPGeometryMode(clear z-buffer, shade, culling, fog, lighting,
        // texture gen, smooth shading; set nothing).
        out.cmd(0xD9000000 | (~(0x00000001u | 0x00000004u | 0x00000600u | 0x00010000u | 0x00020000u | 0x00040000u | 0x00080000u | 0x00200000u) & 0xFFFFFF), 0);
        out.cmd(0xD7000002 | ((uint32_t)tile << 8), 0xFFFFFFFF); // gSPTexture(0xFFFF, 0xFFFF, 0, tile, G_ON)
        // gDPSetTexturePersp(G_TP_PERSP): the HUD draws with G_TP_NONE,
        // under which RT64 halves triangle texture coordinates (as the
        // hardware does without the perspective divide) but not a
        // rectangle's. And gDPSetTextureFilter(G_TF_POINT): the rectangle
        // landed on texel centres, so bilinear filtering never showed; at
        // the in-between positions it would blur the pixel art.
        out.cmd(0x64000019, 0);                        // gEXPushOtherMode
        out.cmd(0xE3000C00, 0x00080000);
        out.cmd(0xE3001201, 0x00000000);
        // gEXMatrixGroupSimple(id, G_EX_PUSH, G_MTX_MODELVIEW, position
        // interpolated, everything else skipped, order linear, edit none).
        out.cmd(0x6400000C, id);
        out.cmd(0x1 | (1u << 3), 0);
        out.cmd(0xDA380002, phys(model));              // gSPMatrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH)
        out.cmd(0x01004008, phys(vtx));                // gSPVertex(vtx, 4, 0)
        out.cmd(0x06000204, 0x00000406);               // gSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0)
        out.cmd(0xD8380002, 64);                       // gSPPopMatrix(G_MTX_MODELVIEW)
        out.cmd(0x6400000D, 1);                        // gEXPopMatrixGroup(G_MTX_MODELVIEW)
        out.cmd(0x6400001A, 0);                        // gEXPopOtherMode
        out.cmd(0xD7000000, 0);                        // gSPTexture off
        out.cmd(0x6400002A, 0);                        // gEXPopGeometryMode
        out.cmd(0x6400001E, 0);                        // gEXPopProjectionMatrix
        out.cmd(0x64000016, 0);                        // gEXPopViewport
        out.cmd(0xDF000000, 0);                        // gSPEndDisplayList
        while (out.addr < viewport) {
            out.cmd(0xDF000000, 0);
        }

        // Full-screen viewport (320 x 240).
        out.h(640); out.h(480); out.h(511); out.h(0);
        out.h(640); out.h(480); out.h(511); out.h(0);

        const float p[4][4] = {
            { 1.0f / 128.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, -1.0f / 128.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f, 0.0f },
            { -1.0f, 1.0f, 0.0f, 1.0f },
        };
        write_mtx(out, p);
        const float m[4][4] = {
            { kScaleX, 0.0f, 0.0f, 0.0f },
            { 0.0f, kScaleY, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f },
            { ulx * kScaleX, uly * kScaleY, 0.0f, 1.0f },
        };
        write_mtx(out, m);

        const int16_t corners[4][4] = {
            { 0, 0, s0, t0 },
            { (int16_t)w, 0, s1, t0 },
            { (int16_t)w, (int16_t)h, s1, t1 },
            { 0, (int16_t)h, s0, t1 },
        };
        for (const auto& c : corners) {
            out.h((uint16_t)c[0]); out.h((uint16_t)c[1]); out.h(0); out.h(0);
            out.h((uint16_t)c[2]); out.h((uint16_t)c[3]);
            out.b(0xFF); out.b(0xFF); out.b(0xFF); out.b(0xFF);
        }
        if (!out.ok()) {
            return false;
        }

        // The rectangle becomes gSPDisplayList(start) and two no-ops.
        MEM_W(0, kseg0(rect)) = (int32_t)0xDE000000;
        MEM_W(4, kseg0(rect)) = (int32_t)phys(start);
        for (int i = 1; i < 3; i++) {
            MEM_W(i * 8, kseg0(rect)) = (int32_t)0xE0000000;
            MEM_W(i * 8 + 4, kseg0(rect)) = 0;
        }
        return true;
    }
}

void btga::markers::new_frame() {
    for (size_t i = 0; i < kMaxCalls; i++) {
        prev_frame[i].swap(cur_frame[i]);
        cur_frame[i].clear();
    }
    call_index = 0;
}

void btga::markers::widget_begin(uint8_t* rdram) {
    in_marker_widget = true;
    widget_start = (uint32_t)MEM_W(0, kseg0(kOverlayDlHeadPtr));
}

void btga::markers::widget_end(uint8_t* rdram) {
    if (!in_marker_widget) {
        return;
    }
    in_marker_widget = false;
    const uint32_t end = (uint32_t)MEM_W(0, kseg0(kOverlayDlHeadPtr));
    const int call = (int)call_index++;
    if (end <= widget_start || end - widget_start > 0x4000 || ((end - widget_start) & 7) != 0) {
        return;
    }

    slot = (slot + 1) % kSlotCount;
    Writer out{ rdram, kScratchStart + slot * kSlotSize, kScratchStart + (slot + 1) * kSlotSize };
    for (uint32_t a = widget_start; a + 24 <= end; a += 8) {
        if (((uint32_t)MEM_W(0, kseg0(a)) >> 24) == 0xE4 &&
            ((uint32_t)MEM_W(8, kseg0(a)) >> 24) == 0xE1 &&
            ((uint32_t)MEM_W(16, kseg0(a)) >> 24) == 0xF1) {
            if (!convert(rdram, a, out, call)) {
                break;
            }
            a += 16;
        }
    }
}
