//
// Lossless Scaling frame generation in RT64's present queue (lib/lsfg).
//
// The host sets the settings (RT64::setFrameGeneration, any thread). With them
// on and RT64 running on Vulkan, the present queue draws each new game frame
// into an image of this class's, which makes frames between it and the last
// one; the queue presents those, spaced over the game frame, then the real
// one (rt64_present_queue.cpp, lib-patches/rt64/0022-frame-generation.patch).
//

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "common/rt64_plume.h"

namespace RT64 {
    struct FrameGenerationSettings {
        bool enabled = false;
        // Frames made between two real ones: 1 doubles the frame rate. 0 is
        // adaptive: as many as the display's refresh rate shows, each made
        // for the moment its refresh shows it (smooth without variable
        // refresh, at any refresh rate).
        uint32_t generatedFrames = 1;
        // Lossless Scaling's performance mode.
        bool performance = false;
        // Lossless.dll, and where to keep its translated shaders.
        std::filesystem::path dllPath;
        std::filesystem::path cachePath;
    };

    // Takes effect on the next game frame.
    void setFrameGeneration(const FrameGenerationSettings &settings);

    // Why frame generation isn't running although it's on (no DLL, not Vulkan,
    // unsupported GPU...); empty when it's off or running.
    std::string getFrameGenerationStatus();

    // Used by the present queue's thread only.
    class FrameGeneration {
    public:
        FrameGeneration();
        ~FrameGeneration();

        // Whether a new game frame is to go through frame generation; sets up
        // what's needed for the swap chain's size and format.
        bool prepare(RenderDevice *device, RenderSwapChain *swapChain, bool vulkan);

        // Where the game frame is to be drawn (COLOR_WRITE).
        RenderFramebuffer *sourceFramebuffer() const;
        RenderTexture *sourceTexture() const;

        // Plans the frames to show for the game frame just drawn into
        // sourceTexture() (now in COPY_SOURCE), from the display's refresh
        // rate (0 if unknown) and the game's frame timing, and records taking
        // it in and generating them. Returns how many frames to present, in
        // order, and the time to leave between them (0: back to back).
        uint32_t record(RenderCommandList *commandList, uint32_t refreshRate, std::chrono::nanoseconds &spacing);

        // Records copying frame `index` of the plan, a generated frame or the
        // game frame itself, to dst (COPY_DEST, the swap chain's size and format).
        void recordFrame(RenderCommandList *commandList, uint32_t index, RenderTexture *dst);

        // Drops the generator (the GPU must be idle).
        void release();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
};
