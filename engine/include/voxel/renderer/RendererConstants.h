#pragma once

#include <volk.h>

#include <cmath>
#include <cstdint>

namespace voxel::renderer
{

/// Maximum number of frames that can be in flight simultaneously.
inline constexpr uint32_t FRAMES_IN_FLIGHT = 2;

/// Maximum number of quads the shared index buffer can address.
inline constexpr uint32_t MAX_QUADS = 2'000'000;

/// Size in bytes of the shared quad index buffer (MAX_QUADS * 6 indices * 4 bytes).
inline constexpr VkDeviceSize QUAD_INDEX_BUFFER_SIZE = static_cast<VkDeviceSize>(MAX_QUADS) * 6 * sizeof(uint32_t);

/// Maximum number of chunk sections that can be resident on the GPU simultaneously.
/// Sufficient for ~20-chunk render distance.
inline constexpr uint32_t MAX_RENDERABLE_SECTIONS = 32'768;

/// Bounding sphere radius for a 16x16x16 chunk section: sqrt(8^2 + 8^2 + 8^2) = sqrt(192).
inline const float SECTION_BOUNDING_RADIUS = std::sqrt(192.0f);

/// Size in bytes of the indirect command buffer (MAX_RENDERABLE_SECTIONS * sizeof(VkDrawIndexedIndirectCommand)).
inline constexpr VkDeviceSize INDIRECT_COMMAND_BUFFER_SIZE =
    static_cast<VkDeviceSize>(MAX_RENDERABLE_SECTIONS) * sizeof(VkDrawIndexedIndirectCommand);

/// Size in bytes of the ChunkRenderInfo SSBO (MAX_RENDERABLE_SECTIONS * 48 bytes per entry).
inline constexpr VkDeviceSize CHUNK_RENDER_INFO_BUFFER_SIZE =
    static_cast<VkDeviceSize>(MAX_RENDERABLE_SECTIONS) * 48;

} // namespace voxel::renderer
