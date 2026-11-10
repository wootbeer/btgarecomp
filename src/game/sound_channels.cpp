// Sound Channels on the Sound tab (round 174): more sounds at once.
//
// The game starts libmus (MusInitialize, func_800FAFBC) once at boot, from
// func_80097560, with a musConfig on its stack at sp+0x10: 42 channels
// (+0x04), a 0x3EBC0-byte heap at 0x801B45D8 (+0x10, +0x14), 0x100 synth
// updates (+0x28), 0x1000 RSP commands (+0x30) and 58 DMA buffers of 0x800
// bytes (+0x38, +0x3C). The synth gets one voice per channel (func_800FF560).
//
// Music and effects share the channels. __MusIntStartEffect (func_800FD8B8)
// takes a free one, or else the playing sound with the lowest priority, if
// that's below the new effect's; otherwise the effect isn't played. That's
// the sounds dropping out in a big fight.
//
// More channels: the count goes up, the updates, RSP commands and DMA
// buffers grow with it, and the heap moves to unused memory above 8 MB
// (0x80800000, 2 MB; the audio microcode reads anywhere in the first 16 MB).
// The game's libmus heap used 0x38220 bytes of its 0x3EBC0 with 42 channels;
// 0x97C00 with 128 and 0x125FF0 with 256.
//
// Measured in Tank Wars, driving and firing (libmus's channel count at
// 0x803AD974 is the config's + 4; channels 0x13C apart from the pointer at
// 0x803AD980, busy while +4 is set): at 42 all 42 were busy the whole time,
// at 128 up to 108, at 256 up to 124. Read once, at boot.
#include <atomic>
#include <cstdint>
#include <cstring>

#include "recomp.h"

#include "btga_config.h"

namespace {
    std::atomic<int> channels{ 0 };

    constexpr int kOriginalChannels = 42;
    constexpr uint32_t kHeap = 0x80800000;
    constexpr uint32_t kHeapSize = 0x200000;

    int32_t scaled(int32_t value, int count) {
        return (int32_t)(((int64_t)value * count + kOriginalChannels - 1) / kOriginalChannels);
    }
}

void btga::set_sound_channels(int count) {
    channels.store(count);
}

// func_80097560 before 0x80097608 (jal MusInitialize): the musConfig is at sp+0x10.
extern "C" void btga_sound_channels(uint8_t* rdram, recomp_context* ctx) {
    const int count = channels.load();
    const gpr config = ctx->r29 + 0x10;
    if (count <= kOriginalChannels || MEM_W(0x04, config) != kOriginalChannels) {
        return;
    }
    std::memset(rdram + (kHeap - 0x80000000u), 0, kHeapSize);
    MEM_W(0x04, config) = count;
    MEM_W(0x10, config) = (int32_t)kHeap;
    MEM_W(0x14, config) = (int32_t)kHeapSize;
    MEM_W(0x28, config) = scaled(MEM_W(0x28, config), count); // synth updates
    MEM_W(0x30, config) = scaled(MEM_W(0x30, config), count); // RSP commands
    MEM_W(0x38, config) = scaled(MEM_W(0x38, config), count); // DMA buffers
}
