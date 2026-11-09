// lsfg::FrameGenerator: lsfg-vk's frame generation contexts on the host's
// device, with the copies in and out the host needs. See lsfg/lsfg.hpp.
#include <volk.h>

#include "lsfg/lsfg.hpp"

#include "common/exception.hpp"
#include "common/utils.hpp"
#include "core/descriptorpool.hpp"
#include "core/device.hpp"
#include "core/image.hpp"
#include "v3_1/context.hpp"
#include "v3_1p/context.hpp"

#include <algorithm>
#include <exception>
#include <optional>

namespace lsfg {

    namespace {
        constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;

        // Moves one of the generator's images between compute use (GENERAL)
        // and a transfer, keeping the layout it tracks up to date.
        void transition(VkCommandBuffer cmd, LSFG::Core::Image& image,
                VkPipelineStageFlags src_stage, VkAccessFlags src_access,
                VkPipelineStageFlags dst_stage, VkAccessFlags dst_access, VkImageLayout layout) {
            const VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = src_access,
                .dstAccessMask = dst_access,
                .oldLayout = image.getLayout(),
                .newLayout = layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image.handle(),
                .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
            };
            vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            image.setLayout(layout);
        }

        void blit(VkCommandBuffer cmd, VkImage src, VkImageLayout src_layout, VkExtent2D src_extent,
                VkImage dst, VkImageLayout dst_layout, VkOffset2D dst_offset, VkExtent2D dst_extent) {
            const VkImageBlit region{
                .srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
                .srcOffsets = { { 0, 0, 0 }, { int32_t(src_extent.width), int32_t(src_extent.height), 1 } },
                .dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
                .dstOffsets = { { dst_offset.x, dst_offset.y, 0 },
                    { dst_offset.x + int32_t(dst_extent.width), dst_offset.y + int32_t(dst_extent.height), 1 } }
            };
            const bool same_size = (src_extent.width == dst_extent.width) && (src_extent.height == dst_extent.height);
            vkCmdBlitImage(cmd, src, src_layout, dst, dst_layout, 1, &region,
                same_size ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
        }
    }

    struct FrameGenerator::Impl {
        Settings settings;
        VkExtent2D extent{};
        // Before the contexts: their descriptor sets come from its pool.
        LSFG::Vulkan vk;
        std::optional<LSFG_3_1::Context> context;
        std::optional<LSFG_3_1P::Context> performance_context;

        LSFG::Core::Image& next_input() {
            return context ? context->nextInput() : performance_context->nextInput();
        }
        std::vector<LSFG::Core::Image>& outputs() {
            return context ? context->outputs() : performance_context->outputs();
        }
        uint64_t frame_count() const {
            return context ? context->frameCount() : performance_context->frameCount();
        }
    };

    float default_flow_scale(uint32_t height) {
        if (height <= 1080) {
            return 1.0f;
        }
        return std::clamp(1080.0f / float(height), 0.25f, 1.0f);
    }

    bool check_device(VkPhysicalDevice physical_device, std::string& reason) {
        VkPhysicalDeviceVulkanMemoryModelFeatures memory_model{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES
        };
        VkPhysicalDeviceFeatures2 features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &memory_model
        };
        vkGetPhysicalDeviceFeatures2(physical_device, &features);
        if (!memory_model.vulkanMemoryModel) {
            reason = "The GPU doesn't support the Vulkan memory model.";
            return false;
        }
        if (!features.features.shaderStorageImageWriteWithoutFormat) {
            reason = "The GPU doesn't support storage image writes without a format.";
            return false;
        }
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(physical_device, kFormat, &format);
        const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if ((format.optimalTilingFeatures & needed) != needed) {
            reason = "The GPU can't use RGBA8 images for frame generation.";
            return false;
        }
        return true;
    }

    std::unique_ptr<FrameGenerator> FrameGenerator::create(VkPhysicalDevice physical_device, VkDevice device,
            const ShaderSet& shaders, const Settings& settings, VkExtent2D extent, std::string& error) {
        if (settings.generated_frames < 1 || extent.width == 0 || extent.height == 0) {
            error = "Invalid frame generation settings.";
            return nullptr;
        }
        try {
            auto impl = std::make_unique<Impl>();
            impl->settings = settings;
            impl->extent = extent;
            const LSFG::Core::Device core_device(physical_device, device);
            impl->vk = LSFG::Vulkan{
                .device = core_device,
                .descriptorPool = LSFG::Core::DescriptorPool(core_device),
                .generationCount = settings.generated_frames,
                .flowScale = std::clamp(settings.flow_scale, 0.25f, 1.0f),
                .isHdr = settings.hdr,
                .shaders = LSFG::Pool::ShaderPool([&shaders](const std::string& name) {
                    auto it = shaders.find(name);
                    return (it != shaders.end()) ? it->second : std::vector<uint8_t>();
                }),
                .resources = LSFG::Pool::ResourcePool(settings.hdr, std::clamp(settings.flow_scale, 0.25f, 1.0f))
            };
            if (settings.performance) {
                impl->performance_context.emplace(impl->vk, extent, kFormat);
            }
            else {
                impl->context.emplace(impl->vk, extent, kFormat);
            }
            // The shaders are compiled into pipelines by now; the set isn't kept.
            impl->vk.shaders = LSFG::Pool::ShaderPool();
            return std::unique_ptr<FrameGenerator>(new FrameGenerator(std::move(impl)));
        }
        catch (const std::exception& e) {
            error = std::string("Frame generation failed to start: ") + e.what();
            return nullptr;
        }
    }

    FrameGenerator::FrameGenerator(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

    FrameGenerator::~FrameGenerator() {
        if (impl) {
            // The contexts' objects go before the pool they come from.
            impl->context.reset();
            impl->performance_context.reset();
        }
    }

    VkExtent2D FrameGenerator::extent() const {
        return impl->extent;
    }

    const Settings& FrameGenerator::settings() const {
        return impl->settings;
    }

    void FrameGenerator::record_input(VkCommandBuffer cmd, VkImage src_image, VkExtent2D src_extent) {
        LSFG::Core::Image& input = impl->next_input();
        transition(cmd, input, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        blit(cmd, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src_extent,
            input.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, { 0, 0 }, impl->extent);
        transition(cmd, input, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
    }

    bool FrameGenerator::record_generate(VkCommandBuffer cmd) {
        const bool has_previous = impl->frame_count() > 0;
        const LSFG::Core::CommandBuffer buffer(cmd);
        if (impl->context) {
            impl->context->record(buffer, impl->settings.generated_frames);
        }
        else {
            impl->performance_context->record(buffer, impl->settings.generated_frames);
        }
        return has_previous;
    }

    void FrameGenerator::record_output(VkCommandBuffer cmd, uint32_t index, VkImage dst_image,
            VkOffset2D dst_offset, VkExtent2D dst_extent) {
        std::vector<LSFG::Core::Image>& outputs = impl->outputs();
        if (index >= outputs.size()) {
            return;
        }
        LSFG::Core::Image& output = outputs[index];
        transition(cmd, output, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        blit(cmd, output.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, impl->extent,
            dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst_offset, dst_extent);
        transition(cmd, output, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    }

}
