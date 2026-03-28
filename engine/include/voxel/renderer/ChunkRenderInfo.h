#pragma once

#include "voxel/renderer/Gigabuffer.h"
#include "voxel/renderer/RendererConstants.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
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
    GigabufferAllocation allocation{};      // Gigabuffer sub-allocation (opaque)
    uint32_t quadCount = 0;                 // Number of opaque quads (for draw command)
    GigabufferAllocation transAllocation{};  // Gigabuffer sub-allocation (translucent)
    uint32_t transQuadCount = 0;             // Number of translucent quads
    glm::ivec3 worldBasePos{0};             // Section world origin (chunkX*16, sectionY*16, chunkZ*16)
    RenderState state = RenderState::None;
};

/// GPU-side per-chunk metadata for compute culling and vertex shader lookups.
/// Layout matches GLSL std430 exactly — see cull.comp / chunk.vert.
struct GpuChunkRenderInfo
{
    glm::vec4 boundingSphere;       // xyz = center (world space), w = radius  (16 bytes, offset 0)
    glm::vec4 worldBasePos;         // xyz = section world origin, w = unused  (16 bytes, offset 16)
    uint32_t gigabufferOffset;      // byte offset into gigabuffer (opaque)    (4 bytes,  offset 32)
    uint32_t quadCount;             // number of opaque quads                  (4 bytes,  offset 36)
    uint32_t transGigabufferOffset; // byte offset into gigabuffer (translucent) (4 bytes, offset 40)
    uint32_t transQuadCount;        // number of translucent quads             (4 bytes,  offset 44)
    uint32_t pad[4];                // explicit padding to 64 bytes            (16 bytes, offset 48)
};
static_assert(sizeof(GpuChunkRenderInfo) == 64, "GpuChunkRenderInfo must be 64 bytes for std430 layout");
static_assert(offsetof(GpuChunkRenderInfo, quadCount) == 36, "quadCount must be at offset 36");

/// Builds a GpuChunkRenderInfo from the CPU-side ChunkRenderInfo.
/// Computes bounding sphere center (section midpoint) and radius.
inline GpuChunkRenderInfo buildGpuInfo(const ChunkRenderInfo& info)
{
    const auto pos = glm::vec3(info.worldBasePos);
    const auto center = pos + glm::vec3(8.0f);
    return GpuChunkRenderInfo{
        .boundingSphere = glm::vec4(center, SECTION_BOUNDING_RADIUS),
        .worldBasePos = glm::vec4(pos, 0.0f),
        .gigabufferOffset = static_cast<uint32_t>(info.allocation.offset),
        .quadCount = info.quadCount,
        .transGigabufferOffset = static_cast<uint32_t>(info.transAllocation.offset),
        .transQuadCount = info.transQuadCount,
        .pad = {0, 0, 0, 0},
    };
}

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
