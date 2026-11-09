//
// Lossless Scaling frame generation in RT64's present queue. See the header.
//

#include "rt64_frame_generation.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

#if !defined(__APPLE__)
#   include "plume_vulkan.h"
#   include "lsfg/lsfg.hpp"
#   define RT64_FRAME_GENERATION
#endif

namespace RT64 {
    namespace {
        std::mutex settingsMutex;
        FrameGenerationSettings pendingSettings;
        uint64_t settingsVersion = 0;
        std::string status;
    }

    void setFrameGeneration(const FrameGenerationSettings &settings) {
        std::scoped_lock lock(settingsMutex);
        pendingSettings = settings;
        settingsVersion++;
        if (!settings.enabled) {
            status.clear();
        }
    }

    std::string getFrameGenerationStatus() {
        std::scoped_lock lock(settingsMutex);
        return status;
    }

    static void setStatus(const std::string &newStatus) {
        std::scoped_lock lock(settingsMutex);
        if (status != newStatus) {
            status = newStatus;
            if (!status.empty()) {
                fprintf(stderr, "Frame generation: %s\n", status.c_str());
            }
        }
    }

    struct FrameGeneration::Impl {
        FrameGenerationSettings settings;
        uint64_t appliedVersion = UINT64_MAX;
        bool failed = false;
#   ifdef RT64_FRAME_GENERATION
        lsfg::ShaderSet shaders;
        std::filesystem::path shadersDll;
        std::unique_ptr<lsfg::FrameGenerator> generator;
#   endif
        RenderDevice *device = nullptr;
        std::unique_ptr<RenderTexture> source;
        std::unique_ptr<RenderFramebuffer> sourceFramebuffer;
        uint32_t width = 0;
        uint32_t height = 0;
        RenderFormat format = RenderFormat::UNKNOWN;

        // Game frame timing, for spacing the generated frames.
        std::chrono::steady_clock::time_point lastGameFrame;
        double intervalNs = 0.0;

        // This game frame's frames: generated frame i, or -1 for the game frame.
        std::vector<int> plan;
        // Adaptive: where the next refresh falls, in game frames from the last one.
        double adaptivePosition = 0.0;
    };

    namespace {
        // Adaptive: the most frames made per game frame (refresh rate / game
        // frame rate; 9 covers 144 Hz down to 16 fps).
        constexpr uint32_t kAdaptiveMaxFrames = 9;
        // Adaptive: a refresh this close to the game frame shows the game frame
        // itself instead of a generated one, in game frames.
        constexpr double kGameFrameMargin = 0.05;
    }

    FrameGeneration::FrameGeneration() : impl(std::make_unique<Impl>()) {}

    FrameGeneration::~FrameGeneration() = default;

    void FrameGeneration::release() {
#   ifdef RT64_FRAME_GENERATION
        impl->generator.reset();
#   endif
        impl->sourceFramebuffer.reset();
        impl->source.reset();
        impl->width = 0;
        impl->height = 0;
    }

    bool FrameGeneration::prepare(RenderDevice *device, RenderSwapChain *swapChain, bool vulkan) {
        {
            std::scoped_lock lock(settingsMutex);
            if (impl->appliedVersion != settingsVersion) {
                const bool changed = (pendingSettings.generatedFrames != impl->settings.generatedFrames) ||
                    (pendingSettings.performance != impl->settings.performance) || !pendingSettings.enabled;
                impl->settings = pendingSettings;
                impl->appliedVersion = settingsVersion;
                impl->failed = false;
                if (changed) {
#               ifdef RT64_FRAME_GENERATION
                    impl->generator.reset();
#               endif
                }
            }
        }

        if (!impl->settings.enabled || impl->failed) {
            return false;
        }

#   ifdef RT64_FRAME_GENERATION
        if (!vulkan) {
            impl->failed = true;
            setStatus("Lossless Scaling frame generation needs the Vulkan graphics API.");
            return false;
        }

        VulkanDevice *vulkanDevice = static_cast<VulkanDevice *>(device);
        const uint32_t width = swapChain->getWidth();
        const uint32_t height = swapChain->getHeight();
        const RenderFormat format = static_cast<VulkanTexture *>(swapChain->getTexture(0))->desc.format;
        if ((width == 0) || (height == 0)) {
            return false;
        }

        if (!vulkanDevice->vulkanMemoryModelSupported) {
            std::string reason;
            impl->failed = true;
            setStatus(lsfg::check_device(vulkanDevice->physicalDevice, reason) ? "The Vulkan memory model wasn't enabled." : reason);
            return false;
        }

        if (impl->shaders.empty() || (impl->shadersDll != impl->settings.dllPath)) {
            std::string error;
            impl->shaders.clear();
            if (impl->settings.dllPath.empty()) {
                error = "Load Lossless.dll first.";
            }
            else {
                lsfg::load_shaders(impl->settings.dllPath, impl->settings.cachePath, impl->shaders, error);
            }

            if (impl->shaders.empty()) {
                impl->failed = true;
                setStatus(error);
                return false;
            }

            impl->shadersDll = impl->settings.dllPath;
        }

        // The game frame is drawn into an image of the swap chain's size and format.
        if ((impl->source == nullptr) || (impl->device != device) || (impl->width != width) || (impl->height != height) || (impl->format != format)) {
            impl->generator.reset();
            impl->sourceFramebuffer.reset();
            impl->source = device->createTexture(RenderTextureDesc::ColorTarget(width, height, format));
            const RenderTexture *sourceTexture = impl->source.get();
            impl->sourceFramebuffer = device->createFramebuffer(RenderFramebufferDesc(&sourceTexture, 1));
            impl->device = device;
            impl->width = width;
            impl->height = height;
            impl->format = format;
        }

        if (impl->generator == nullptr) {
            lsfg::Settings settings;
            settings.generated_frames = (impl->settings.generatedFrames == 0) ? kAdaptiveMaxFrames : std::clamp(impl->settings.generatedFrames, 1U, 7U);
            settings.performance = impl->settings.performance;
            settings.flow_scale = lsfg::default_flow_scale(height);
            std::string error;
            impl->generator = lsfg::FrameGenerator::create(vulkanDevice->physicalDevice, vulkanDevice->vk, impl->shaders, settings, { width, height }, error);
            if (impl->generator == nullptr) {
                impl->failed = true;
                setStatus(error);
                return false;
            }

            impl->lastGameFrame = {};
            impl->intervalNs = 0.0;
            impl->adaptivePosition = 0.0;
        }

        setStatus(std::string());
        return true;
#   else
        (void)(device); (void)(swapChain); (void)(vulkan);
        impl->failed = true;
        setStatus("Lossless Scaling frame generation isn't available on this platform.");
        return false;
#   endif
    }

    RenderFramebuffer *FrameGeneration::sourceFramebuffer() const {
        return impl->sourceFramebuffer.get();
    }

    RenderTexture *FrameGeneration::sourceTexture() const {
        return impl->source.get();
    }

    uint32_t FrameGeneration::record(RenderCommandList *commandList, uint32_t refreshRate, std::chrono::nanoseconds &spacing) {
        spacing = std::chrono::nanoseconds(0);
        impl->plan.clear();

        // The game's frame interval, averaged. Past 250 ms it's a hitch or a pause, not the frame rate.
        const auto now = std::chrono::steady_clock::now();
        if (impl->lastGameFrame != std::chrono::steady_clock::time_point()) {
            const double interval = double(std::chrono::duration_cast<std::chrono::nanoseconds>(now - impl->lastGameFrame).count());
            if (interval < 250'000'000.0) {
                impl->intervalNs = (impl->intervalNs > 0.0) ? (impl->intervalNs * 0.75 + interval * 0.25) : interval;
            }
        }

        impl->lastGameFrame = now;

#   ifdef RT64_FRAME_GENERATION
        // The points in time between the last game frame (0) and this one (1) to make frames for.
        std::vector<float> timestamps;
        bool showGameFrame = true;
        const double refreshNs = (refreshRate > 0) ? (1'000'000'000.0 / double(refreshRate)) : 0.0;
        const bool adaptive = (impl->settings.generatedFrames == 0) && (refreshNs > 0.0) && (impl->intervalNs > 0.0);
        if (adaptive) {
            // One frame per refresh: each made for the moment its refresh shows it, with the game frame
            // itself when a refresh falls on it. Over time that's exactly the refresh rate.
            const double step = refreshNs / impl->intervalNs;
            double &position = impl->adaptivePosition;
            if (position <= 0.0) {
                position = step;
            }

            while ((position < (1.0 - kGameFrameMargin)) && (timestamps.size() < kAdaptiveMaxFrames)) {
                timestamps.push_back(float(position));
                position += step;
            }

            showGameFrame = (position <= (1.0 + kGameFrameMargin));
            if (showGameFrame) {
                position += step;
            }

            position = std::max(position - 1.0, 0.0);
            spacing = std::chrono::nanoseconds(int64_t(refreshNs));
        }
        else {
            // A fixed number of frames, evenly spaced over the game frame.
            const uint32_t count = (impl->settings.generatedFrames == 0) ? 1 : impl->generator->settings().generated_frames;
            for (uint32_t i = 0; i < count; i++) {
                timestamps.push_back(float(i + 1) / float(count + 1));
            }

            if (impl->intervalNs > 0.0) {
                spacing = std::chrono::nanoseconds(int64_t(impl->intervalNs / double(count + 1)));
            }
        }

        VkCommandBuffer cmd = static_cast<VulkanCommandList *>(commandList)->vk;
        VkImage sourceImage = static_cast<VulkanTexture *>(impl->source.get())->vk;
        impl->generator->record_input(cmd, sourceImage, { impl->width, impl->height });
        const bool generated = impl->generator->record_generate(cmd, timestamps);
        if (generated) {
            for (uint32_t i = 0; i < timestamps.size(); i++) {
                impl->plan.push_back(int(i));
            }
        }

        // The first frame after the generator is made has nothing before it: only the game frame.
        if (showGameFrame || impl->plan.empty()) {
            impl->plan.push_back(-1);
        }

        if (!generated) {
            spacing = std::chrono::nanoseconds(0);
        }

        return uint32_t(impl->plan.size());
#   else
        (void)(commandList); (void)(refreshRate);
        impl->plan.push_back(-1);
        return 1;
#   endif
    }

    void FrameGeneration::recordFrame(RenderCommandList *commandList, uint32_t index, RenderTexture *dst) {
        if ((index >= impl->plan.size()) || (impl->plan[index] < 0)) {
            commandList->copyTexture(dst, impl->source.get());
            return;
        }

#   ifdef RT64_FRAME_GENERATION
        VkCommandBuffer cmd = static_cast<VulkanCommandList *>(commandList)->vk;
        VkImage dstImage = static_cast<VulkanTexture *>(dst)->vk;
        impl->generator->record_output(cmd, uint32_t(impl->plan[index]), dstImage, { 0, 0 }, { impl->width, impl->height });
#   endif
    }
};
