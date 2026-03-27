#pragma once

#include "voxel/core/Result.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <vector>

namespace voxel::renderer
{

/// Builder for VkDescriptorSetLayout with chained API.
class DescriptorLayoutBuilder
{
public:
    /// Add a binding to the layout being constructed.
    DescriptorLayoutBuilder& addBinding(
        uint32_t binding,
        VkDescriptorType type,
        VkShaderStageFlags stageFlags,
        uint32_t count = 1);

    /// Build the descriptor set layout from accumulated bindings.
    [[nodiscard]] core::Result<VkDescriptorSetLayout> build(VkDevice device);

    /// Clear all bindings for reuse.
    void clear();

private:
    std::vector<VkDescriptorSetLayoutBinding> m_bindings;
};

/// Manages VkDescriptorPool allocation with automatic pool growth.
class DescriptorAllocator
{
public:
    explicit DescriptorAllocator(VkDevice device);
    ~DescriptorAllocator();

    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
    DescriptorAllocator(DescriptorAllocator&&) = delete;
    DescriptorAllocator& operator=(DescriptorAllocator&&) = delete;

    /// Allocate a descriptor set from the managed pools.
    [[nodiscard]] core::Result<VkDescriptorSet> allocate(VkDescriptorSetLayout layout);

    /// Reset all pools. Invalidates all previously allocated descriptor sets.
    void resetPools();

private:
    core::Result<VkDescriptorPool> createPool();

    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_usedPools;
    std::vector<VkDescriptorPool> m_freePools;
    VkDescriptorPool m_currentPool = VK_NULL_HANDLE;

    static constexpr uint32_t SETS_PER_POOL = 1000;
};

} // namespace voxel::renderer
