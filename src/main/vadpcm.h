// VADPCM, the N64's ADPCM sample format, as this game's audio uses it: order 2
// codebooks with 4 predictors, 9-byte frames of 16 samples (a header byte with
// the scale shift and predictor, then 4-bit residuals, predicted in halves of
// 8). Used by the BattleTanx 1 effects (bt1_sounds.cpp) and sound packs
// (sound_packs.cpp).
#pragma once

#include <cstdint>
#include <vector>

namespace btga::vadpcm {
    constexpr int kOrder = 2;
    constexpr int kPredictors = 4;
    constexpr int kFrameSamples = 16;
    constexpr int kFrameBytes = 9;
    // An ALADPCMBook's coefficients (after its order and predictor count).
    constexpr int kBookBytes = kOrder * kPredictors * 8 * 2;

    struct Book {
        int32_t c[kPredictors][kOrder][8];
    };

    // From an ALADPCMBook (order, predictor count, then big-endian coefficients).
    Book read_book(const uint8_t* book);
    // Its coefficients, big-endian, to an ALADPCMBook's (after the 8-byte header).
    void write_book_coefficients(const Book& book, uint8_t* coefficients);

    // hist: the last two samples decoded (older, newer), updated.
    void decode_frame(const Book& book, const uint8_t frame[kFrameBytes], int32_t hist[2], int32_t out[kFrameSamples]);
    // The frame (predictor and scale with the least error) closest to target,
    // from hist, which is updated to what the frame decodes to.
    void encode_frame(const Book& book, const int32_t target[kFrameSamples], int32_t hist[2], uint8_t frame[kFrameBytes]);

    // A codebook fitted to the sound: 4 second-order predictors, each the least
    // squares fit to the frames it predicts best (generalized Lloyd, split from
    // one predictor).
    Book design_book(const std::vector<int16_t>& pcm);

    // The sound encoded from silence, padded with silence to whole frames.
    std::vector<uint8_t> encode(const Book& book, const std::vector<int16_t>& pcm);
    // The encoded sound decoded, as the game plays it.
    std::vector<int16_t> decode(const Book& book, const std::vector<uint8_t>& data);
}
