#pragma once

#include <cstdint>

namespace voxel::renderer
{

/// Maximum number of frames that can be in flight simultaneously.
inline constexpr uint32_t FRAMES_IN_FLIGHT = 2;

} // namespace voxel::renderer
