#include "voxel/renderer/ParticleManager.h"

#include "voxel/core/Log.h"
#include "voxel/world/Block.h"
#include "voxel/world/ChunkManager.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <random>

namespace voxel::renderer
{

void ParticleManager::init(VmaAllocator allocator)
{
    // 4 vertices per particle, 32 bytes each
    VkDeviceSize bufferSize = MAX_PARTICLES * 4 * sizeof(ParticleVertex);

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mappedInfo{};
    VkResult result = vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &m_vertexBuffer, &m_allocation, &mappedInfo);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create particle vertex buffer: {}", static_cast<int>(result));
        return;
    }

    m_mapped = static_cast<ParticleVertex*>(mappedInfo.pMappedData);
    m_isInitialized = true;
    VX_LOG_INFO("ParticleManager initialized ({} KB vertex buffer)", bufferSize / 1024);
}

void ParticleManager::shutdown(VmaAllocator allocator)
{
    if (m_vertexBuffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator, m_vertexBuffer, m_allocation);
        m_vertexBuffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mapped = nullptr;
    }
    m_particles.clear();
    m_isInitialized = false;
}

void ParticleManager::addParticle(const Particle& p)
{
    if (m_particles.size() >= MAX_PARTICLES)
    {
        // Evict oldest (front)
        m_particles.erase(m_particles.begin());
    }
    m_particles.push_back(p);
}

void ParticleManager::addParticleSpawner(
    uint32_t amount,
    const glm::vec3& minPos,
    const glm::vec3& maxPos,
    const glm::vec3& minVel,
    const glm::vec3& maxVel,
    uint16_t textureLayer,
    float size,
    float lifetime)
{
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> distX(minPos.x, maxPos.x);
    std::uniform_real_distribution<float> distY(minPos.y, maxPos.y);
    std::uniform_real_distribution<float> distZ(minPos.z, maxPos.z);
    std::uniform_real_distribution<float> velX(minVel.x, maxVel.x);
    std::uniform_real_distribution<float> velY(minVel.y, maxVel.y);
    std::uniform_real_distribution<float> velZ(minVel.z, maxVel.z);

    for (uint32_t i = 0; i < amount; ++i)
    {
        Particle p;
        p.pos = {distX(rng), distY(rng), distZ(rng)};
        p.velocity = {velX(rng), velY(rng), velZ(rng)};
        p.acceleration = {0.0f, -9.81f, 0.0f};
        p.lifetime = lifetime;
        p.maxLifetime = lifetime;
        p.textureLayer = textureLayer;
        p.size = size;
        p.collide = false;
        addParticle(p);
    }
}

void ParticleManager::update(float dt, world::ChunkManager* chunkMgr)
{
    for (auto it = m_particles.begin(); it != m_particles.end();)
    {
        it->velocity += it->acceleration * dt;
        glm::vec3 newPos = it->pos + it->velocity * dt;

        // Simple collision: if collide flag set and new position is inside a solid block, stop
        if (it->collide && chunkMgr != nullptr)
        {
            glm::ivec3 blockPos = glm::ivec3(glm::floor(newPos));
            uint16_t blockId = chunkMgr->getBlock(blockPos);
            if (blockId != 0) // not air
            {
                it->velocity = glm::vec3(0.0f);
                // Don't update position — particle stays at current pos
            }
            else
            {
                it->pos = newPos;
            }
        }
        else
        {
            it->pos = newPos;
        }

        it->lifetime -= dt;
        if (it->lifetime <= 0.0f)
        {
            it = m_particles.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ParticleManager::uploadVertices(const glm::vec3& cameraRight, const glm::vec3& cameraUp)
{
    if (!m_isInitialized || m_mapped == nullptr || m_particles.empty())
    {
        return;
    }

    uint32_t count = std::min(static_cast<uint32_t>(m_particles.size()), MAX_PARTICLES);
    ParticleVertex* dst = m_mapped;

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& p = m_particles[i];
        float halfSize = p.size * 0.5f;
        float alpha = p.lifetime / p.maxLifetime;
        float layer = static_cast<float>(p.textureLayer);

        glm::vec3 right = cameraRight * halfSize;
        glm::vec3 up = cameraUp * halfSize;

        // Bottom-left
        dst->pos = p.pos - right - up;
        dst->uv_x = 0.0f;
        dst->uv_y = 0.0f;
        dst->texLayer = layer;
        dst->alpha = alpha;
        dst->pad = 0.0f;
        ++dst;

        // Bottom-right
        dst->pos = p.pos + right - up;
        dst->uv_x = 1.0f;
        dst->uv_y = 0.0f;
        dst->texLayer = layer;
        dst->alpha = alpha;
        dst->pad = 0.0f;
        ++dst;

        // Top-right
        dst->pos = p.pos + right + up;
        dst->uv_x = 1.0f;
        dst->uv_y = 1.0f;
        dst->texLayer = layer;
        dst->alpha = alpha;
        dst->pad = 0.0f;
        ++dst;

        // Top-left
        dst->pos = p.pos - right + up;
        dst->uv_x = 0.0f;
        dst->uv_y = 1.0f;
        dst->texLayer = layer;
        dst->alpha = alpha;
        dst->pad = 0.0f;
        ++dst;
    }
}

} // namespace voxel::renderer
