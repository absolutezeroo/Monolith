#include "voxel/renderer/DescriptorAllocator.h"

#include "voxel/core/Log.h"

#include <array>

namespace voxel::renderer
{

// --- DescriptorLayoutBuilder ---

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addBinding(
    uint32_t binding,
    VkDescriptorType type,
    VkShaderStageFlags stageFlags,
    uint32_t count)
{
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = type;
    layoutBinding.descriptorCount = count;
    layoutBinding.stageFlags = stageFlags;
    layoutBinding.pImmutableSamplers = nullptr;

    m_bindings.push_back(layoutBinding);
    return *this;
}

core::Result<VkDescriptorSetLayout> DescriptorLayoutBuilder::build(VkDevice device)
{
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(m_bindings.size());
    layoutInfo.pBindings = m_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create descriptor set layout: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create descriptor set layout"));
    }

    return layout;
}

void DescriptorLayoutBuilder::clear()
{
    m_bindings.clear();
}

// --- DescriptorAllocator ---

DescriptorAllocator::DescriptorAllocator(VkDevice device) : m_device(device) {}

DescriptorAllocator::~DescriptorAllocator()
{
    for (auto pool : m_freePools)
    {
        vkDestroyDescriptorPool(m_device, pool, nullptr);
    }
    for (auto pool : m_usedPools)
    {
        vkDestroyDescriptorPool(m_device, pool, nullptr);
    }
    if (m_currentPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device, m_currentPool, nullptr);
    }
}

core::Result<VkDescriptorSet> DescriptorAllocator::allocate(VkDescriptorSetLayout layout)
{
    // Ensure we have a current pool
    if (m_currentPool == VK_NULL_HANDLE)
    {
        if (!m_freePools.empty())
        {
            m_currentPool = m_freePools.back();
            m_freePools.pop_back();
        }
        else
        {
            auto poolResult = createPool();
            if (!poolResult.has_value())
            {
                return std::unexpected(poolResult.error());
            }
            m_currentPool = poolResult.value();
        }
    }

    // Try allocating from the current pool
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_currentPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet);

    if (result == VK_SUCCESS)
    {
        return descriptorSet;
    }

    // Pool is full — move current pool to used, get or create a new one
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
    {
        m_usedPools.push_back(m_currentPool);
        m_currentPool = VK_NULL_HANDLE;

        if (!m_freePools.empty())
        {
            m_currentPool = m_freePools.back();
            m_freePools.pop_back();
        }
        else
        {
            auto poolResult = createPool();
            if (!poolResult.has_value())
            {
                return std::unexpected(poolResult.error());
            }
            m_currentPool = poolResult.value();
        }

        // Retry with new pool
        allocInfo.descriptorPool = m_currentPool;
        result = vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet);
        if (result == VK_SUCCESS)
        {
            return descriptorSet;
        }
    }

    VX_LOG_ERROR("Failed to allocate descriptor set: {}", static_cast<int>(result));
    return std::unexpected(
        core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to allocate descriptor set"));
}

void DescriptorAllocator::resetPools()
{
    for (auto pool : m_usedPools)
    {
        vkResetDescriptorPool(m_device, pool, 0);
        m_freePools.push_back(pool);
    }
    m_usedPools.clear();

    if (m_currentPool != VK_NULL_HANDLE)
    {
        vkResetDescriptorPool(m_device, m_currentPool, 0);
        m_freePools.push_back(m_currentPool);
        m_currentPool = VK_NULL_HANDLE;
    }
}

core::Result<VkDescriptorPool> DescriptorAllocator::createPool()
{
    std::array<VkDescriptorPoolSize, 4> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SETS_PER_POOL * 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SETS_PER_POOL},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SETS_PER_POOL},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SETS_PER_POOL / 2},
    }};

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = SETS_PER_POOL;
    poolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolCI.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorPool(m_device, &poolCI, nullptr, &pool);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create descriptor pool: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create descriptor pool"));
    }

    VX_LOG_DEBUG("Created descriptor pool (maxSets={})", SETS_PER_POOL);
    return pool;
}

} // namespace voxel::renderer
