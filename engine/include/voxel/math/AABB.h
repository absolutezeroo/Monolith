#pragma once

#include "voxel/math/MathTypes.h"

#include <glm/common.hpp>

namespace voxel::math
{

struct AABB
{
    Vec3 min{0.0f};
    Vec3 max{0.0f};

    /**
     * @brief Tests whether a point lies inside or on the boundary of this AABB.
     * @param point The point to test.
     * @return true if point is contained (inclusive on all faces).
     */
    [[nodiscard]] inline bool contains(Vec3 point) const
    {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y && point.z >= min.z &&
               point.z <= max.z;
    }

    /**
     * @brief Tests whether this AABB overlaps another (touching counts as intersecting).
     * @param other The other AABB to test against.
     * @return true if the AABBs overlap or touch.
     */
    [[nodiscard]] inline bool intersects(const AABB& other) const
    {
        return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    /**
     * @brief Expands this AABB to include the given point.
     * @param point The point to include.
     * @return Reference to this AABB for chaining.
     */
    inline AABB& expand(Vec3 point)
    {
        min = glm::min(min, point);
        max = glm::max(max, point);
        return *this;
    }

    /**
     * @brief Returns the center point of this AABB.
     */
    [[nodiscard]] inline Vec3 center() const { return (min + max) * 0.5f; }

    /**
     * @brief Returns the half-widths (extents) of this AABB along each axis.
     */
    [[nodiscard]] inline Vec3 extents() const { return (max - min) * 0.5f; }
};

} // namespace voxel::math
