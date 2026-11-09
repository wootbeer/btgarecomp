#include <volk.h>

#include "pool/resourcepool.hpp"
#include "core/buffer.hpp"
#include "core/device.hpp"
#include "core/sampler.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace LSFG;
using namespace LSFG::Pool;

struct ConstantBuffer {
    std::array<uint32_t, 2> inputOffset;
    uint32_t firstIter;
    uint32_t firstIterS;
    uint32_t advancedColorKind;
    uint32_t hdrSupport;
    float resolutionInvScale;
    float timestamp;
    float uiThreshold;
    std::array<uint32_t, 3> pad;
};

Core::Buffer ResourcePool::getBuffer(
            const Core::Device& device,
            float timestamp, bool firstIter, bool firstIterS) {
    uint64_t hash = 0;
    const union { float f; uint32_t i; } u{
        .f = timestamp };
    hash |= u.i;
    hash |= static_cast<uint64_t>(firstIter) << 32;
    hash |= static_cast<uint64_t>(firstIterS) << 33;

    auto it = buffers.find(hash);
    if (it != buffers.end())
        return it->second;

    // create the buffer
    const ConstantBuffer data{
        .inputOffset = { 0, 0 },
        .advancedColorKind = this->isHdr ? 2U : 0U,
        .hdrSupport = this->isHdr,
        .resolutionInvScale = this->flowScale,
        .timestamp = timestamp,
        .uiThreshold = 0.5F,
    };
    Core::Buffer buffer(device, data, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    buffers[hash] = buffer;
    return buffer;
}

Core::Buffer ResourcePool::getPassBuffer(
            const Core::Device& device, size_t pass,
            float timestamp, bool firstIter, bool firstIterS) {
    const uint64_t key = (static_cast<uint64_t>(pass) << 2) |
        (static_cast<uint64_t>(firstIter) << 0) | (static_cast<uint64_t>(firstIterS) << 1);
    auto it = passBuffers.find(key);
    if (it != passBuffers.end())
        return it->second;

    const ConstantBuffer data{
        .inputOffset = { 0, 0 },
        // firstIter and firstIterS stay 0, as lsfg-vk's getBuffer() leaves them.
        .advancedColorKind = this->isHdr ? 2U : 0U,
        .hdrSupport = this->isHdr,
        .resolutionInvScale = this->flowScale,
        .timestamp = timestamp,
        .uiThreshold = 0.5F,
    };
    Core::Buffer buffer(device, data, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    passBuffers[key] = buffer;
    passTimestamps[pass] = timestamp;
    return buffer;
}

void ResourcePool::setPassTimestamp(const Core::Device& device, size_t pass, float timestamp) {
    auto current = passTimestamps.find(pass);
    if (current == passTimestamps.end() || current->second == timestamp)
        return;
    current->second = timestamp;
    for (uint64_t flags = 0; flags < 4; flags++) {
        auto it = passBuffers.find((static_cast<uint64_t>(pass) << 2) | flags);
        if (it == passBuffers.end())
            continue;
        void* mapped = nullptr;
        if (vkMapMemory(device.handle(), it->second.getMemory(), 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
            continue;
        std::memcpy(static_cast<uint8_t*>(mapped) + offsetof(ConstantBuffer, timestamp), &timestamp, sizeof(float));
        vkUnmapMemory(device.handle(), it->second.getMemory());
    }
}

Core::Sampler ResourcePool::getSampler(
            const Core::Device& device,
            VkSamplerAddressMode type,
            VkCompareOp compare,
            bool isWhite) {
    uint64_t hash = 0;
    hash |= static_cast<uint64_t>(type) << 0;
    hash |= static_cast<uint64_t>(compare) << 8;
    hash |= static_cast<uint64_t>(isWhite) << 16;

    auto it = samplers.find(hash);
    if (it != samplers.end())
        return it->second;

    // create the sampler
    Core::Sampler sampler(device, type, compare, isWhite);
    samplers[hash] = sampler;
    return sampler;
}
