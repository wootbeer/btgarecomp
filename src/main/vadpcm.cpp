#include "vadpcm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace btga::vadpcm {
    namespace {
        uint16_t read_be16(const uint8_t* p) {
            return (uint16_t)(p[0] << 8 | p[1]);
        }

        // One half frame from 8 residuals (already scaled); prev0, prev1 = the two samples before.
        void decode_half(const Book& b, int p, const int32_t r[8], int32_t prev0, int32_t prev1, int32_t out[8]) {
            for (int i = 0; i < 8; i++) {
                int64_t t = (int64_t)b.c[p][0][i] * prev0 + (int64_t)b.c[p][1][i] * prev1;
                for (int k = 0; k < i; k++) {
                    t += (int64_t)b.c[p][1][i - 1 - k] * r[k];
                }
                out[i] = std::clamp<int32_t>((int32_t)(t >> 11) + r[i], -32768, 32767);
            }
        }

        // x[n] = a1 x[n-1] + a2 x[n-2], kept stable (both poles inside the unit circle).
        struct Predictor {
            double a1 = 0.0, a2 = 0.0;
        };

        Predictor stabilize(Predictor p) {
            p.a2 = std::clamp(p.a2, -0.98, 0.98);
            const double limit = (1.0 - p.a2) - 0.02;
            p.a1 = std::clamp(p.a1, -limit, limit);
            return p;
        }

        // Per frame: the sums least squares needs (x the sample, h1 h2 the two before).
        struct FrameStats {
            double xx = 0, xh1 = 0, xh2 = 0, h1h1 = 0, h1h2 = 0, h2h2 = 0;

            void add(const FrameStats& o) {
                xx += o.xx; xh1 += o.xh1; xh2 += o.xh2; h1h1 += o.h1h1; h1h2 += o.h1h2; h2h2 += o.h2h2;
            }

            // Residual energy predicting with p.
            double error(const Predictor& p) const {
                return xx - 2.0 * (p.a1 * xh1 + p.a2 * xh2) + p.a1 * p.a1 * h1h1 + 2.0 * p.a1 * p.a2 * h1h2 + p.a2 * p.a2 * h2h2;
            }

            Predictor solve() const {
                // Regularized so silent or constant frames don't make it singular.
                const double r = 1e-6 * (h1h1 + h2h2) + 1e-3;
                const double a = h1h1 + r, b = h1h2, d = h2h2 + r;
                const double det = a * d - b * b;
                if (std::fabs(det) < 1e-12) {
                    return {};
                }
                return stabilize({ (xh1 * d - xh2 * b) / det, (xh2 * a - xh1 * b) / det });
            }
        };

        // A predictor's 8x2 codebook entry: the samples it predicts from the
        // two before (h2 for the older, the impulse response g for the newer
        // and for the residuals), in Q11.
        void predictor_to_book(const Predictor& p, int32_t c[2][8]) {
            double g[9];
            g[0] = 1.0;
            g[1] = p.a1;
            for (int n = 2; n < 9; n++) {
                g[n] = p.a1 * g[n - 1] + p.a2 * g[n - 2];
            }
            double h2[8];
            h2[0] = p.a2;
            h2[1] = p.a1 * p.a2;
            for (int n = 2; n < 8; n++) {
                h2[n] = p.a1 * h2[n - 1] + p.a2 * h2[n - 2];
            }
            for (int i = 0; i < 8; i++) {
                c[0][i] = std::clamp<int32_t>((int32_t)std::lround(h2[i] * 2048.0), -32768, 32767);
                c[1][i] = std::clamp<int32_t>((int32_t)std::lround(g[i + 1] * 2048.0), -32768, 32767);
            }
        }
    }

    Book read_book(const uint8_t* book) {
        Book b;
        for (int p = 0; p < kPredictors; p++) {
            for (int o = 0; o < kOrder; o++) {
                for (int i = 0; i < 8; i++) {
                    b.c[p][o][i] = (int16_t)read_be16(book + 8 + ((p * kOrder + o) * 8 + i) * 2);
                }
            }
        }
        return b;
    }

    void write_book_coefficients(const Book& book, uint8_t* coefficients) {
        for (int p = 0; p < kPredictors; p++) {
            for (int o = 0; o < kOrder; o++) {
                for (int i = 0; i < 8; i++) {
                    const uint16_t v = (uint16_t)(int16_t)book.c[p][o][i];
                    uint8_t* d = coefficients + ((p * kOrder + o) * 8 + i) * 2;
                    d[0] = (uint8_t)(v >> 8);
                    d[1] = (uint8_t)v;
                }
            }
        }
    }

    void decode_frame(const Book& b, const uint8_t f[kFrameBytes], int32_t hist[2], int32_t out[kFrameSamples]) {
        const int scale = f[0] >> 4, p = std::min(f[0] & 0xF, kPredictors - 1);
        for (int half = 0; half < 2; half++) {
            int32_t r[8];
            for (int i = 0; i < 8; i++) {
                int n = (f[1 + half * 4 + i / 2] >> ((i & 1) ? 0 : 4)) & 0xF;
                r[i] = (n >= 8 ? n - 16 : n) << scale;
            }
            decode_half(b, p, r, hist[0], hist[1], out + half * 8);
            hist[0] = out[half * 8 + 6];
            hist[1] = out[half * 8 + 7];
        }
    }

    void encode_frame(const Book& b, const int32_t target[kFrameSamples], int32_t hist[2], uint8_t f[kFrameBytes]) {
        int64_t best_err = INT64_MAX;
        for (int p = 0; p < kPredictors; p++) {
            for (int scale = 0; scale <= 12; scale++) {
                int32_t h[2] = { hist[0], hist[1] }, out[kFrameSamples];
                int nib[kFrameSamples];
                for (int half = 0; half < 2; half++) {
                    int32_t r[8] = {};
                    for (int i = 0; i < 8; i++) {
                        // The prediction for sample i, given the residuals so far.
                        int32_t trial[8];
                        r[i] = 0;
                        decode_half(b, p, r, h[0], h[1], trial);
                        const double want = double(target[half * 8 + i] - trial[i]) / double(1 << scale);
                        nib[half * 8 + i] = std::clamp((int)std::lround(want), -8, 7);
                        r[i] = nib[half * 8 + i] << scale;
                    }
                    decode_half(b, p, r, h[0], h[1], out + half * 8);
                    h[0] = out[half * 8 + 6];
                    h[1] = out[half * 8 + 7];
                }
                int64_t err = 0;
                for (int i = 0; i < kFrameSamples; i++) {
                    err += (int64_t)(out[i] - target[i]) * (out[i] - target[i]);
                }
                if (err < best_err) {
                    best_err = err;
                    f[0] = (uint8_t)(scale << 4 | p);
                    for (int i = 0; i < 8; i++) {
                        f[1 + i] = (uint8_t)((nib[2 * i] & 0xF) << 4 | (nib[2 * i + 1] & 0xF));
                    }
                }
            }
        }
        int32_t out[kFrameSamples];
        decode_frame(b, f, hist, out);
    }

    Book design_book(const std::vector<int16_t>& pcm) {
        // Per-frame statistics, leaving out frames too quiet to matter.
        std::vector<FrameStats> frames;
        for (size_t start = 0; start + kFrameSamples <= pcm.size(); start += kFrameSamples) {
            FrameStats s;
            for (size_t n = start; n < start + kFrameSamples; n++) {
                const double x = pcm[n];
                const double h1 = (n >= 1) ? pcm[n - 1] : 0.0;
                const double h2 = (n >= 2) ? pcm[n - 2] : 0.0;
                s.xx += x * x; s.xh1 += x * h1; s.xh2 += x * h2;
                s.h1h1 += h1 * h1; s.h1h2 += h1 * h2; s.h2h2 += h2 * h2;
            }
            if (s.xx > kFrameSamples * 4.0) {
                frames.push_back(s);
            }
        }

        std::vector<Predictor> predictors;
        {
            FrameStats all;
            for (const FrameStats& s : frames) {
                all.add(s);
            }
            predictors.push_back(all.solve());
        }

        // Split each predictor in two and refine, until there are 4.
        std::vector<int> owner(frames.size(), 0);
        while ((int)predictors.size() < kPredictors) {
            const size_t count = predictors.size();
            for (size_t k = 0; k < count; k++) {
                Predictor a = predictors[k], b = predictors[k];
                a.a1 += 0.05; b.a1 -= 0.05;
                a.a2 -= 0.03; b.a2 += 0.03;
                predictors[k] = stabilize(a);
                predictors.push_back(stabilize(b));
            }
            for (int iteration = 0; iteration < 20; iteration++) {
                std::vector<FrameStats> pooled(predictors.size());
                std::vector<double> load(predictors.size(), 0.0);
                for (size_t f = 0; f < frames.size(); f++) {
                    size_t best = 0;
                    double best_error = frames[f].error(predictors[0]);
                    for (size_t k = 1; k < predictors.size(); k++) {
                        const double e = frames[f].error(predictors[k]);
                        if (e < best_error) {
                            best_error = e;
                            best = k;
                        }
                    }
                    owner[f] = (int)best;
                    pooled[best].add(frames[f]);
                    load[best] += best_error;
                }
                for (size_t k = 0; k < predictors.size(); k++) {
                    if (pooled[k].h1h1 > 0.0) {
                        predictors[k] = pooled[k].solve();
                    }
                    else {
                        // Nothing chose it: restart it next to the predictor with the most error.
                        const size_t worst = (size_t)(std::max_element(load.begin(), load.end()) - load.begin());
                        predictors[k] = stabilize({ predictors[worst].a1 + 0.1, predictors[worst].a2 - 0.05 });
                    }
                }
            }
        }

        Book book;
        for (int p = 0; p < kPredictors; p++) {
            predictor_to_book(predictors[p], book.c[p]);
        }
        return book;
    }

    std::vector<uint8_t> encode(const Book& book, const std::vector<int16_t>& pcm) {
        const size_t frames = (pcm.size() + kFrameSamples - 1) / kFrameSamples;
        std::vector<uint8_t> data(frames * kFrameBytes);
        int32_t hist[2] = { 0, 0 };
        for (size_t f = 0; f < frames; f++) {
            int32_t target[kFrameSamples] = {};
            for (int i = 0; i < kFrameSamples; i++) {
                const size_t n = f * kFrameSamples + i;
                target[i] = (n < pcm.size()) ? pcm[n] : 0;
            }
            encode_frame(book, target, hist, data.data() + f * kFrameBytes);
        }
        return data;
    }

    std::vector<int16_t> decode(const Book& book, const std::vector<uint8_t>& data) {
        std::vector<int16_t> pcm;
        pcm.reserve(data.size() / kFrameBytes * kFrameSamples);
        int32_t hist[2] = { 0, 0 };
        for (size_t pos = 0; pos + kFrameBytes <= data.size(); pos += kFrameBytes) {
            int32_t out[kFrameSamples];
            decode_frame(book, data.data() + pos, hist, out);
            pcm.insert(pcm.end(), out, out + kFrameSamples);
        }
        return pcm;
    }
}
