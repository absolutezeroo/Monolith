#pragma once

#include <volk.h>

#include <cstdint>

namespace voxel::renderer
{

/// Maximum number of frames that can be in flight simultaneously.
inline constexpr uint32_t FRAMES_IN_FLIGHT = 2;

/// Maximum number of quads the shared index buffer can address.
inline constexpr uint32_t MAX_QUADS = 2'000'000;

/// Size in bytes of the shared quad index buffer (MAX_QUADS * 6 indices * 4 bytes).
inline constexpr VkDeviceSize QUAD_INDEX_BUFFER_SIZE = static_cast<VkDeviceSize>(MAX_QUADS) * 6 * sizeof(uint32_t);

} // namespace voxel::renderer
