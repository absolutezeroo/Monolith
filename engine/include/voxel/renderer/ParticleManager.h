#pragma once

#include <vk_mem_alloc.h>
#include <volk.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace voxel::world
{
class ChunkManager;
}

namespace voxel::renderer
{

/// A single live particle in the simulation.
struct Particle
{
    glm::vec3 pos{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 acceleration{0.0f, -9.81f, 0.0f};
    float lifetime = 1.0f;
    float maxLifetime = 1.0f;
    uint16_t textureLayer = 0;
    float size = 0.1f;
    bool collide = false;
};

/// Per-vertex data for particle billboard quads (4 vertices per particle).
struct ParticleVertex
{
    glm::vec3 pos;    // 12 bytes — world-space corner position
    float uv_x;       // 4 bytes
    float uv_y;       // 4 bytes
    float texLayer;   // 4 bytes — texture array layer
    float alpha;      // 4 bytes — lifetime-based fade
    float pad;        // 4 bytes — alignment
};
static_assert(sizeof(ParticleVertex) == 32);

/**
 * @brief Manages up to MAX_PARTICLES active particles.
 *
 * Updates positions each frame (gravity via acceleration, lifetime countdown).
 * Oldest particles are killed when budget is exceeded.
 * Renders as forward-blended billboard quads after the deferred lighting pass.
 */
class ParticleManager
{
public:
    static constexpr uint32_t MAX_PARTICLES = 2000;

    /// Creates GPU vertex buffer (HOST_VISIBLE, persistently mapped).
    void init(VmaAllocator allocator);

    /// Destroys GPU buffer. Must be called before VMA/device destruction.
    void shutdown(VmaAllocator allocator);

    /// Adds a single particle. Evicts oldest if at budget.
    void addParticle(const Particle& p);

    /// Immediately spawns `amount` particles spread across the given region.
    /// V1: no persistent spawner handle.
    void addParticleSpawner(
        uint32_t amount,
        const glm::vec3& minPos,
        const glm::vec3& maxPos,
        const glm::vec3& minVel,
        const glm::vec3& maxVel,
        uint16_t textureLayer,
        float size,
        float lifetime);

    /// Advances particle simulation: position += velocity * dt, velocity += accel * dt, etc.
    void update(float dt, world::ChunkManager* chunkMgr = nullptr);

    /// Writes billboard quad vertices to the mapped GPU buffer.
    void uploadVertices(const glm::vec3& cameraRight, const glm::vec3& cameraUp);

    [[nodiscard]] uint32_t getActiveCount() const { return static_cast<uint32_t>(m_particles.size()); }
    [[nodiscard]] VkBuffer getVertexBuffer() const { return m_vertexBuffer; }

private:
    std::vector<Particle> m_particles;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    ParticleVertex* m_mapped = nullptr;
    bool m_isInitialized = false;
};

} // namespace voxel::renderer
