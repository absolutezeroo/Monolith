#pragma once

#include "voxel/math/MathTypes.h"

namespace voxel::math
{

/**
 * @brief A ray defined by an origin and direction.
 *
 * The direction MUST be normalized by the caller. This struct does not
 * enforce normalization to avoid overhead in hot-path code (e.g., DDA raycasting).
 */
struct Ray
{
    Vec3 origin{0.0f};
    Vec3 direction{0.0f, 0.0f, 1.0f};
};

} // namespace voxel::math
