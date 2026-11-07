// Sound Effects Reverb on the Sound tab (round 162): a little more of the
// game's own reverb on the sound effects. The music stays as it is.
//
// The synth has a reverb: func_800FE778 (this game's copy of n_alSynNew; the
// n_alSynNew at 0x80102350 is never called) gets fxType 6, a custom room of
// the game's own at 0x801266B8. Each voice's share of it is its FX mix,
// 0-127, which libmus sends as a parameter update. n_alEnvmixerPull applies
// it at 0x801003C0: dry = eqpower[mix], wet = eqpower[127 - mix], from the
// update's +0xC. Measured in Tank Wars, the music's voices send 20-50 and the
// effects mostly 0, a few 10-50.
//
// The effects are told apart from the music by where their samples come
// from: the effects' sample data is at ROM 0x593A28 up to the music's bank
// at 0x61A698, and the first game's effects (src/main/bt1_sounds.cpp) are
// past the end of the 8 MB ROM. Wave bases are cartridge addresses
// (0xB0000000 + ROM offset). The voice's wave table is its load filter's
// +0x20, the envmixer and load filter being the same voice.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "recomp.h"

#include "btga_config.h"

namespace {
    std::atomic<bool> enabled{ false };
    std::atomic<int32_t> amount{ 0 };

    // The most the slider adds: 30% of the full 127, as Turok Rage Wars'
    // Weapon Reverb.
    constexpr float kMaxAmount = 0.3f * 127.0f;

    constexpr uint32_t kCartBase = 0xB0000000;
    constexpr uint32_t kSfxSamples = 0x593A28;
    constexpr uint32_t kMusicBank = 0x61A698;
    constexpr uint32_t kRomSize = 0x800000;

    bool is_sound_effect(uint32_t cart_address) {
        if (cart_address < kCartBase) {
            return false;
        }
        const uint32_t rom = cart_address - kCartBase;
        return (rom >= kSfxSamples && rom < kMusicBank) || rom >= kRomSize;
    }
}

void btga::set_sfx_reverb(bool on, double percent) {
    enabled.store(on);
    amount.store((int32_t)std::lround(std::clamp(percent / 100.0, 0.0, 1.0) * kMaxAmount));
}

// n_alEnvmixerPull before 0x801003C0: $a0 is an FX mix update (the mix at
// +0xC), $s0 the voice.
extern "C" void btga_sfx_reverb(uint8_t* rdram, recomp_context* ctx) {
    if (!enabled.load()) {
        return;
    }
    const uint32_t wave = (uint32_t)MEM_W(0x20, ctx->r16);
    if (wave < 0x80000000u || wave >= 0x80800000u) {
        return;
    }
    if (!is_sound_effect((uint32_t)MEM_W(0, (gpr)(int32_t)wave))) {
        return;
    }
    const int32_t mix = MEM_W(0xC, ctx->r4);
    MEM_W(0xC, ctx->r4) = std::clamp(mix + amount.load(), 0, 127);
}
