// TEMPORARY round 116 diagnostic: render-mode fingerprint of a frame.
// Walks the gfx task display list every 2 s (from the per-VI scissor hook)
// and counts triangles per distinct (othermode H, othermode L, combiner,
// geometry mode) state, flagging the modes RT64 renders with per-frame
// randomness or special depth handling: alpha-compare dither, alpha/colour
// dither noise, decal z mode.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>
#include <algorithm>

#include "recomp.h"

namespace {
    struct ModeKey {
        uint32_t om_h, om_l, cc0, cc1, geom;
        bool operator<(const ModeKey& o) const {
            return std::tie(om_h, om_l, cc0, cc1, geom) < std::tie(o.om_h, o.om_l, o.cc0, o.cc1, o.geom);
        }
    };

    struct ModeScan {
        uint8_t* rdram;
        uint32_t seg[16] = {};
        int cmds = 0;
        ModeKey cur{};
        std::map<ModeKey, int> tris;

        static constexpr uint32_t kRamMask = 0x7FFFFF;
        uint32_t phys(uint32_t a) const { return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & kRamMask; }
        uint32_t w32(uint32_t p) const { return *(uint32_t*)(rdram + (p & kRamMask)); }

        static void apply_othermode(uint32_t& word, uint32_t w0, uint32_t w1) {
            uint32_t len = (w0 & 0xFF) + 1, shift = 32 - ((w0 >> 8) & 0xFF) - len;
            uint32_t mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << shift;
            word = (word & ~mask) | (w1 & mask);
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
                case 0xE3: apply_othermode(cur.om_h, w0, w1); break;
                case 0xE2: apply_othermode(cur.om_l, w0, w1); break;
                case 0xEF: cur.om_h = w0 & 0x00FFFFFF; cur.om_l = w1; break;
                case 0xFC: cur.cc0 = w0 & 0x00FFFFFF; cur.cc1 = w1; break;
                case 0xD9: cur.geom = (cur.geom & (w0 & 0x00FFFFFF)) | w1; break; // G_GEOMETRYMODE: and-clear, or-set
                case 0x05: tris[cur] += 1; break;
                case 0x06: case 0x07: tris[cur] += 2; break;
                }
            }
        }
    };
}

extern "C" void btga_render_mode_diag(uint8_t* rdram) {
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(3)) return;
    last = now;
    uint32_t dl = *(uint32_t*)(rdram + (0x801293E0u - 0x80000000u) + 0x30);
    if (dl < 0x80000000u || dl >= 0x80800000u) return;
    ModeScan s{ rdram };
    s.walk(dl, 0);
    std::vector<std::pair<int, ModeKey>> sorted;
    for (auto& [k, n] : s.tris) sorted.push_back({ n, k });
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.first > b.first; });
    printf("[BTGA MODE] states=%zu\n", sorted.size());
    for (auto& [n, k] : sorted) {
        uint32_t ac = k.om_l & 3;                // alpha compare: 0 none, 1 threshold, 3 dither
        uint32_t zmode = (k.om_l >> 10) & 3;     // 3 = decal
        uint32_t ad = (k.om_h >> 4) & 3;         // alpha dither: 2 = noise
        uint32_t cd = (k.om_h >> 6) & 3;         // colour dither: 2 = noise
        printf("[BTGA MODE]   tris=%d H=%06X L=%08X cc=%06X:%08X geom=%06X%s%s%s%s\n", n, k.om_h, k.om_l, k.cc0, k.cc1, k.geom,
            ac == 3 ? " AC_DITHER" : ac == 1 ? " AC_THRESH" : "", zmode == 3 ? " ZDECAL" : "",
            ad == 2 ? " AD_NOISE" : "", cd == 2 ? " CD_NOISE" : "");
    }
    fflush(stdout);
}
