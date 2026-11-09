#pragma once

#include "core/device.hpp"
#include "core/buffer.hpp"
#include "core/sampler.hpp"

#include "vulkan/vulkan_core.h"

#include <cstdint>
#include <unordered_map>

namespace LSFG::Pool {

    ///
    /// Resource pool for each Vulkan device.
    ///
    class ResourcePool {
    public:
        ResourcePool() noexcept = default;

        ///
        /// Create the resource pool.
        ///
        /// @param isHdr HDR support stored in buffers.
        /// @param flowScale Scale factor stored in buffers.
        ///
        /// @throws std::runtime_error if the resource pool cannot be created.
        ///
        ResourcePool(bool isHdr, float flowScale)
            : isHdr(isHdr), flowScale(flowScale) {}

        ///
        /// Retrieve a buffer with given parameters or create it.
        ///
        /// @param timestamp Timestamp stored in buffer
        /// @param firstIter First iteration stored in buffer
        /// @param firstIterS First special iteration stored in buffer
        /// @return Created or cached buffer
        ///
        /// @throws LSFG::vulkan_error if the buffer cannot be created.
        ///
        Core::Buffer getBuffer(
            const Core::Device& device,
            float timestamp = 0.0F, bool firstIter = false, bool firstIterS = false);

        ///
        /// Retrieve a sampler by type or create it.
        ///
        /// @param type Type of the sampler
        /// @param compare Compare operation for the sampler
        /// @param isWhite Whether the sampler is white
        /// @return Created or cached sampler
        ///
        /// @throws LSFG::vulkan_error if the sampler cannot be created.
        ///
        ///
        /// Get the constant buffer of generation pass `pass` (BattleTanx GA
        /// port: one per pass and flags, not shared by timestamp, so that
        /// setPassTimestamp() can move a pass to another point in time).
        ///
        Core::Buffer getPassBuffer(
            const Core::Device& device, size_t pass,
            float timestamp, bool firstIter = false, bool firstIterS = false);

        ///
        /// Set the point in time generation pass `pass` makes a frame for
        /// (0 is the previous frame, 1 the next). The GPU must be done with
        /// the pass's last frame.
        ///
        void setPassTimestamp(const Core::Device& device, size_t pass, float timestamp);

        Core::Sampler getSampler(
            const Core::Device& device,
            VkSamplerAddressMode type = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            VkCompareOp compare = VK_COMPARE_OP_NEVER,
            bool isWhite = false);

    private:
        std::unordered_map<uint64_t, Core::Buffer> buffers;
        std::unordered_map<uint64_t, Core::Sampler> samplers;
        std::unordered_map<uint64_t, Core::Buffer> passBuffers; // by pass << 2 | flags
        std::unordered_map<size_t, float> passTimestamps;
        bool isHdr{};
        float flowScale{};
    };

}
