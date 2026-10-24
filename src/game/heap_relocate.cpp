// A bigger heap for the game's level data (round 150).
//
// The game loads each level's data with one bump allocator, func_800ACEB4:
// free bytes at 0x8023A064 and the next address at 0x8023A068, set up and
// cleared by func_800ACE70 (boot) and func_800AF43C (each level load) as
// 0x15D5D8 bytes (1.4 MB) from 0x8023A070. It returns 0 when a request
// doesn't fit and its callers don't check: the decompressor (func_800A0750)
// then writes through the null pointer. Normal play never gets there, but
// the All Tanks All Levels code makes every level load every tank type
// (about 160 KB more in mission 1), and a player crashed at the end of the
// White House and Brandenburg Gate missions with it on. The N64 would fail
// the same way.
//
// The game never uses the upper 4 MB of the 8 MB it's given (checked: still
// all zero through boot, the intro and a level load), so right after each
// setup the heap is moved there with all of it, 4 MB instead of 1.4 MB.
// Everything else reads the two variables, including the game's own
// save-and-restore of them, so nothing else changes.
#include <cstdint>
#include <cstring>

#include "recomp.h"

namespace {
    constexpr uint32_t kHeapFree = 0x8023A064;
    constexpr uint32_t kHeapNext = 0x8023A068;
    constexpr uint32_t kHeapStart = 0x80400000;
    constexpr uint32_t kHeapSize = 0x400000;
}

// After the heap setup's bzero in func_800ACE70 (0x800ACEA4) and
// func_800AF43C (0x800AF488).
extern "C" void btga_heap_setup(uint8_t* rdram) {
    std::memset(rdram + (kHeapStart - 0x80000000u), 0, kHeapSize);
    MEM_W(0, (gpr)(int32_t)kHeapNext) = (int32_t)kHeapStart;
    MEM_W(0, (gpr)(int32_t)kHeapFree) = (int32_t)kHeapSize;
}
