// TEMPORARY round 118 diagnostic: frame-to-frame display-list diff.
// At each frame clear (start of frame N+1) walks the display list frame N
// built from its clear onward, reduces it to a structural token list
// (commands and state words; per-frame buffer addresses blanked; vertex
// colour/normal bytes hashed in), and prints, per frame, a summary line plus
// the differing span against frame N-1. Also prints the first projection
// matrix's z/w columns and the fog word, to catch numeric jumps.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "recomp.h"

namespace {
    constexpr uint32_t kRamMask = 0x7FFFFF;

    struct Token {
        uint32_t w0, w1, extra;
        bool operator==(const Token& o) const { return w0 == o.w0 && w1 == o.w1 && extra == o.extra; }
    };

    struct Walker {
        uint8_t* rdram;
        uint32_t seg[16] = {};
        int cmds = 0;
        std::vector<Token> tokens;
        int tris = 0, branch_z = 0, cull_dl = 0;
        uint32_t fog_word = 0;
        bool have_proj = false;
        float proj[4][4] = {};

        uint32_t phys(uint32_t a) const {
            if ((a & 0xFF000000u) == 0x80000000u) return a & kRamMask;
            return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & kRamMask;
        }
        uint32_t w32(uint32_t p) const { return *(uint32_t*)(rdram + (p & kRamMask & ~3u)); }
        uint32_t h16(uint32_t p) const { uint32_t w = w32(p); return (p & 2) ? (w & 0xFFFF) : (w >> 16); }
        uint32_t b8(uint32_t p) const { uint32_t w = w32(p); return (w >> (24 - 8 * (p & 3))) & 0xFF; }

        void read_matrix(uint32_t p) {
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    int idx = i * 4 + j;
                    int32_t v = (int32_t)((h16(p + idx * 2) << 16) | h16(p + 32 + idx * 2));
                    proj[i][j] = v / 65536.0f;
                }
            }
            have_proj = true;
        }

        void walk(uint32_t addr, int depth) {
            if (depth > 18) return;
            uint32_t p = phys(addr);
            while (cmds++ < 300000) {
                uint32_t w0 = w32(p), w1 = w32(p + 4);
                p += 8;
                uint32_t op = w0 >> 24;
                Token t{ w0, w1, 0 };
                switch (op) {
                case 0xDE:
                    t.w1 = (w1 & 0xFF000000u) == 0x80000000u ? 0 : w1;
                    tokens.push_back(t);
                    walk(w1, depth + 1);
                    if (((w0 >> 16) & 0xFF) != 0) return;
                    continue;
                case 0xDF: tokens.push_back(t); return;
                case 0xDB:
                    if (((w0 >> 16) & 0xFF) == 0x06) { seg[((w0 & 0xFFFF) / 4) & 0xF] = w1 & kRamMask; t.w1 = 0; }
                    if (((w0 >> 16) & 0xFF) == 0x08) fog_word = w1;
                    break;
                case 0xDA:
                    if (!have_proj && (w0 & 0x04)) read_matrix(phys(w1));
                    t.w1 = (w1 & 0xFF000000u) == 0x80000000u ? 0 : w1;
                    break;
                case 0xDC: t.w1 = 0; break;
                case 0x01: {
                    uint32_t n = (w0 >> 12) & 0xFF;
                    uint32_t v = phys(w1);
                    uint32_t h = 2166136261u;
                    for (uint32_t i = 0; i < n; i++) { h = (h ^ w32(v + i * 16 + 12)) * 16777619u; }
                    t.extra = h;
                    t.w1 = (w1 & 0xFF000000u) == 0x80000000u ? 0 : w1;
                    break;
                }
                case 0x03: cull_dl++; break;
                case 0x04: branch_z++; break;
                case 0x05: tris += 1; break;
                case 0x06: case 0x07: tris += 2; break;
                case 0xFD: case 0xFF: case 0xFE:
                    t.w1 = (w1 & 0xFF000000u) == 0x80000000u ? 0 : w1;
                    break;
                }
                tokens.push_back(t);
            }
        }
    };

    std::vector<Token> prev_tokens;
    uint32_t pending_start = 0;
    int frame = 0;
    int printed_diff_frames = 0;

    const char* op_name(uint32_t op) {
        switch (op) {
        case 0x01: return "VTX"; case 0x03: return "CULLDL"; case 0x04: return "BRANCHZ";
        case 0x05: return "TRI1"; case 0x06: return "TRI2"; case 0x07: return "QUAD";
        case 0xD7: return "TEXTURE"; case 0xD9: return "GEOM"; case 0xDA: return "MTX"; case 0xD8: return "POPMTX";
        case 0xDB: return "MOVEWORD"; case 0xDC: return "MOVEMEM"; case 0xDE: return "DL"; case 0xDF: return "ENDDL";
        case 0xE1: return "RDPHALF1"; case 0xE2: return "OM_L"; case 0xE3: return "OM_H"; case 0xE7: return "PIPESYNC";
        case 0xE4: return "TEXRECT"; case 0xF5: return "SETTILE"; case 0xF3: return "LOADBLOCK"; case 0xFD: return "SETTIMG";
        case 0xFA: return "PRIM"; case 0xFB: return "ENV"; case 0xF8: return "FOGCOL"; case 0xF7: return "FILLCOL";
        case 0xFC: return "COMBINE"; case 0xF6: return "FILLRECT"; case 0xEF: return "OTHERMODE"; case 0xED: return "SCISSOR";
        }
        return "?";
    }


    struct Edit { char kind; size_t ai, bi; };

    // Myers O(ND) diff; false if more than kMaxD edits.
    bool myers_diff(const std::vector<Token>& a, const std::vector<Token>& b, std::vector<Edit>& out) {
        const int n = (int)a.size(), m = (int)b.size();
        const int kMaxD = 800;
        const int off = kMaxD + 1;
        std::vector<int> v(2 * off + 1, 0);
        std::vector<std::vector<int>> trace;
        int found_d = -1;
        for (int d = 0; d <= kMaxD; d++) {
            trace.push_back(std::vector<int>(v.begin() + off - d - 1, v.begin() + off + d + 2));
            for (int k = -d; k <= d; k += 2) {
                int x;
                if (k == -d || (k != d && v[off + k - 1] < v[off + k + 1])) x = v[off + k + 1];
                else x = v[off + k - 1] + 1;
                int y = x - k;
                while (x < n && y < m && a[x] == b[y]) { x++; y++; }
                v[off + k] = x;
                if (x >= n && y >= m) { found_d = d; break; }
            }
            if (found_d >= 0) break;
        }
        if (found_d < 0) return false;
        // Backtrack.
        int x = n, y = m;
        std::vector<Edit> rev;
        for (int d = found_d; d > 0; d--) {
            const std::vector<int>& pv = trace[d]; // v before step d, covering k in [-d-1, d+1]
            auto V = [&](int k) { return pv[k + d + 1]; };
            int k = x - y;
            int prev_k = (k == -d || (k != d && V(k - 1) < V(k + 1))) ? k + 1 : k - 1;
            int prev_x = V(prev_k), prev_y = prev_x - prev_k;
            while (x > prev_x && y > prev_y) { x--; y--; }
            if (x == prev_x) rev.push_back({ '+', (size_t)prev_x, (size_t)prev_y });
            else rev.push_back({ '-', (size_t)prev_x, (size_t)prev_y });
            x = prev_x; y = prev_y;
        }
        out.assign(rev.rbegin(), rev.rend());
        return true;
    }

    void print_token(char tag, size_t i, const Token& t) {
        printf("[BTGA DLD]   %c%5zu %-9s %08X %08X %08X\n", tag, i, op_name(t.w0 >> 24), t.w0, t.w1, t.extra);
    }
}

extern "C" void btga_dl_diff_diag(uint8_t* rdram, recomp_context* ctx) {
    uint32_t head = (uint32_t)MEM_W(0x24, ctx->r30);
    uint32_t start = pending_start;
    pending_start = head - 16;
    if (start < 0x80000000u || start >= 0x80800000u) return;
    frame++;

    Walker w{ rdram };
    w.walk(start, 0);

    printf("[BTGA DLD] f=%d start=%08X cmds=%zu tris=%d bz=%d cull=%d fog=%08X proj z=(%.4f %.4f %.2f %.2f) w=(%.4f %.4f %.2f %.2f)\n",
        frame, start, w.tokens.size(), w.tris, w.branch_z, w.cull_dl, w.fog_word,
        w.proj[0][2], w.proj[1][2], w.proj[2][2], w.proj[3][2], w.proj[0][3], w.proj[1][3], w.proj[2][3], w.proj[3][3]);

    const std::vector<Token>& a = prev_tokens;
    const std::vector<Token>& b = w.tokens;
    if (!a.empty()) {
        std::vector<Edit> edits;
        if (!myers_diff(a, b, edits)) {
            printf("[BTGA DLD]  diff too large (old %zu, new %zu cmds)\n", a.size(), b.size());
        }
        else if (!edits.empty()) {
            int removed = 0, added = 0;
            for (const Edit& e : edits) (e.kind == '-' ? removed : added)++;
            printf("[BTGA DLD]  diff: -%d +%d\n", removed, added);
            if (printed_diff_frames < 600) {
                printed_diff_frames++;
                // Print up to 8 hunks of consecutive edits, 12 lines each.
                size_t i = 0;
                int hunks = 0;
                while (i < edits.size() && hunks < 8) {
                    size_t j = i;
                    while (j + 1 < edits.size() && edits[j + 1].ai <= edits[j].ai + 1 && edits[j + 1].bi <= edits[j].bi + 1) j++;
                    printf("[BTGA DLD]  @@ old %zu new %zu\n", edits[i].ai, edits[i].bi);
                    if (edits[i].bi > 0) print_token(' ', edits[i].bi - 1, b[edits[i].bi - 1]);
                    for (size_t k = i; k <= j && k < i + 12; k++) {
                        const Edit& e = edits[k];
                        print_token(e.kind, e.kind == '-' ? e.ai : e.bi, e.kind == '-' ? a[e.ai] : b[e.bi]);
                    }
                    if (j - i + 1 > 12) printf("[BTGA DLD]    ... %zu more\n", j - i + 1 - 12);
                    i = j + 1;
                    hunks++;
                }
            }
        }
    }
    prev_tokens = std::move(w.tokens);
    fflush(stdout);
}
