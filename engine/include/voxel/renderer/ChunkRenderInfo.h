#pragma once

#include "voxel/renderer/Gigabuffer.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace voxel::renderer
{

/// GPU-side render state for a chunk section's mesh allocation.
enum class RenderState : uint8_t
{
    None,        // No mesh data (empty section or not yet uploaded)
    Resident,    // Mesh data in gigabuffer, ready for draw
    PendingFree, // Old allocation queued for deferred free
};

/// GPU-side render metadata for one chunk section.
struct ChunkRenderInfo
{
    GigabufferAllocation allocation{}; // Gigabuffer sub-allocation
    uint32_t quadCount = 0;           // Number of quads (for draw command)
    glm::ivec3 worldBasePos{0};       // Section world origin (chunkX*16, sectionY*16, chunkZ*16)
    RenderState state = RenderState::None;
};

/// Key for identifying a chunk section in the render info map.
struct SectionKey
{
    glm::ivec2 chunkCoord{0, 0};
    int sectionY = 0;

    bool operator==(const SectionKey& other) const
    {
        return chunkCoord == other.chunkCoord && sectionY == other.sectionY;
    }
};

/// Hash for SectionKey used in unordered containers.
struct SectionKeyHash
{
    size_t operator()(const SectionKey& k) const noexcept
    {
        size_t h = std::hash<int>{}(k.chunkCoord.x);
        h ^= std::hash<int>{}(k.chunkCoord.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.sectionY) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

using ChunkRenderInfoMap = std::unordered_map<SectionKey, ChunkRenderInfo, SectionKeyHash>;

} // namespace voxel::renderer
