// TEMPORARY round 109 diagnostic: what the frame's display list really
// loads as projection matrices, and how often draws use z-compare /
// z-update / "depth source = primitive" (G_ZS_PRIM, flat depth from
// G_SETPRIMDEPTH). Prints every 2 s from btga_fix_screen_edge_scissors
// (every VI). Remove once the cutscene flicker is understood.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

#include "recomp.h"

namespace {
    struct DlProjScan {
        uint8_t* rdram;
        uint32_t seg[16] = {};
        int cmds = 0;
        uint32_t othermode_l = 0;
        int draws = 0, draws_zcmp = 0, draws_zupd = 0, draws_zprim = 0;
        uint32_t prim_depth = 0;
        std::set<std::string> projections, prim_depths;

        static constexpr uint32_t kRamMask = 0x7FFFFF;
        uint32_t phys(uint32_t a) const { return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & kRamMask; }
        uint32_t w32(uint32_t p) const { return *(uint32_t*)(rdram + (p & kRamMask)); }
        int16_t h16(uint32_t p) const { return *(int16_t*)(rdram + ((p & kRamMask) ^ 2)); }
        uint16_t u16(uint32_t p) const { return *(uint16_t*)(rdram + ((p & kRamMask) ^ 2)); }

        float mtx(uint32_t m, int row, int col) const {
            uint32_t i = (row * 4 + col) * 2;
            return (float)((int32_t)((uint32_t)h16(m + i) << 16 | u16(m + 32 + i))) / 65536.0f;
        }

        void count_draw() {
            draws++;
            if (othermode_l & 0x10) draws_zcmp++;  // Z_CMP
            if (othermode_l & 0x20) draws_zupd++;  // Z_UPD
            if (othermode_l & 0x04) draws_zprim++; // G_ZS_PRIM
        }

        void walk(uint32_t addr, int depth) {
            if (depth > 18) return;
            uint32_t p = phys(addr);
            while (cmds++ < 200000) {
                uint32_t w0 = w32(p), w1 = w32(p + 4);
                p += 8;
                switch (w0 >> 24) {
                case 0xDE:
                    walk(w1, depth + 1);
                    if (((w0 >> 16) & 0xFF) != 0) return;
                    break;
                case 0xDF: return;
                case 0xDB:
                    if (((w0 >> 16) & 0xFF) == 0x06) seg[((w0 & 0xFFFF) / 4) & 0xF] = w1 & kRamMask;
                    break;
                case 0xDA: // G_MTX
                    if ((w0 & 0x04) != 0) { // G_MTX_PROJECTION
                        uint32_t m = phys(w1);
                        char buf[200];
                        snprintf(buf, sizeof(buf), "m00=%.3f m11=%.3f m22=%.4f m23=%.3f m32=%.3f m33=%.3f",
                            mtx(m, 0, 0), mtx(m, 1, 1), mtx(m, 2, 2), mtx(m, 2, 3), mtx(m, 3, 2), mtx(m, 3, 3));
                        projections.insert(buf);
                    }
                    break;
                case 0xE2: // G_SETOTHERMODE_L
                {
                    uint32_t len = (w0 & 0xFF) + 1, shift = 32 - ((w0 >> 8) & 0xFF) - len;
                    uint32_t mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << shift;
                    othermode_l = (othermode_l & ~mask) | (w1 & mask);
                    break;
                }
                case 0xEF: othermode_l = w1; break; // G_RDPSETOTHERMODE
                case 0xEE: // G_SETPRIMDEPTH
                {
                    char buf[40];
                    snprintf(buf, sizeof(buf), "%04X", w1 >> 16);
                    prim_depths.insert(buf);
                    break;
                }
                case 0x05: case 0x06: case 0x07: // tri1 / tri2 / quad
                case 0xE4: case 0xE5:            // texrect
                    count_draw();
                    break;
                }
            }
        }
    };
}

extern "C" void btga_projection_diag(uint8_t* rdram) {
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(2)) return;
    last = now;
    uint32_t dl = *(uint32_t*)(rdram + (0x801293E0u - 0x80000000u) + 0x30);
    if (dl < 0x80000000u || dl >= 0x80800000u) return;
    DlProjScan s{ rdram };
    s.walk(dl, 0);
    printf("[BTGA PROJ] draws=%d zcmp=%d zupd=%d zprim=%d far_var=%d\n", s.draws, s.draws_zcmp, s.draws_zupd, s.draws_zprim,
        *(int32_t*)(rdram + (0x8023A060u - 0x80000000u)));
    for (auto& p : s.projections) printf("[BTGA PROJ]   proj %s\n", p.c_str());
    for (auto& d : s.prim_depths) printf("[BTGA PROJ]   primdepth %s\n", d.c_str());
    fflush(stdout);
}

// TEMPORARY round 112 diagnostic: which map objects func_800AF978 draws
// each frame (object record + chosen LOD mesh list, recorded right before
// its func_8007B1F0 draw call at 0x800AFCD4), and how that set changes from
// frame to frame. Printed every frame from btga_frame_dt.
#include <map>
#include <utility>

namespace {
    std::set<std::pair<uint32_t, uint32_t>> objs_cur, objs_prev;
    int frame_index = 0;
}

extern "C" void btga_obj_diag_draw(uint8_t* rdram, recomp_context* ctx) {
    objs_cur.insert({ (uint32_t)ctx->r16, (uint32_t)ctx->r19 });
}

extern "C" void btga_obj_diag_frame(uint8_t* rdram) {
    int added = 0, removed = 0, lod_changed = 0;
    std::map<uint32_t, uint32_t> prev_lod;
    for (auto& [obj, lod] : objs_prev) prev_lod[obj] = lod;
    std::set<uint32_t> cur_objs;
    for (auto& [obj, lod] : objs_cur) {
        cur_objs.insert(obj);
        auto it = prev_lod.find(obj);
        if (it == prev_lod.end()) added++;
        else if (it->second != lod) lod_changed++;
    }
    for (auto& [obj, lod] : prev_lod) {
        if (!cur_objs.count(obj)) removed++;
    }
    float cx = *(float*)(rdram + (0x802194B4u - 0x80000000u));
    float cz = *(float*)(rdram + (0x802194B8u - 0x80000000u));
    if (!objs_cur.empty() || !objs_prev.empty()) {
        printf("[BTGA OBJ] f=%d drawn=%zu added=%d removed=%d lodchg=%d cam=%.2f,%.2f\n",
            frame_index, cur_objs.size(), added, removed, lod_changed, cx, cz);
        fflush(stdout);
    }
    frame_index++;
    objs_prev.swap(objs_cur);
    objs_cur.clear();
}
