#include "voxel/physics/Raycast.h"

#include "voxel/scripting/ShapeCache.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace voxel::physics
{

bool rayIntersectsAABB(
    const glm::vec3& origin,
    const glm::vec3& invDir,
    const math::AABB& box,
    float& tMin)
{
    float t1 = (box.min.x - origin.x) * invDir.x;
    float t2 = (box.max.x - origin.x) * invDir.x;
    float t3 = (box.min.y - origin.y) * invDir.y;
    float t4 = (box.max.y - origin.y) * invDir.y;
    float t5 = (box.min.z - origin.z) * invDir.z;
    float t6 = (box.max.z - origin.z) * invDir.z;

    float tNear = std::max({std::min(t1, t2), std::min(t3, t4), std::min(t5, t6)});
    float tFar = std::min({std::max(t1, t2), std::max(t3, t4), std::max(t5, t6)});

    if (tNear > tFar || tFar < 0.0f)
    {
        return false;
    }

    tMin = tNear >= 0.0f ? tNear : tFar;
    return true;
}

RaycastResult raycast(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    const world::ChunkManager& world,
    const world::BlockRegistry& registry,
    scripting::ShapeCache* shapeCache)
{
    using renderer::BlockFace;

    // Precompute inverse direction for ray-AABB tests
    glm::vec3 invDir;
    invDir.x = (direction.x != 0.0f) ? (1.0f / direction.x) : FLT_MAX;
    invDir.y = (direction.y != 0.0f) ? (1.0f / direction.y) : FLT_MAX;
    invDir.z = (direction.z != 0.0f) ? (1.0f / direction.z) : FLT_MAX;

    // 1. Starting voxel = floor(origin)
    glm::ivec3 blockPos = glm::ivec3(glm::floor(origin));
    glm::ivec3 prevPos = blockPos;

    // 2. Step direction per axis: +1 or -1
    glm::ivec3 step;
    step.x = (direction.x >= 0.0f) ? 1 : -1;
    step.y = (direction.y >= 0.0f) ? 1 : -1;
    step.z = (direction.z >= 0.0f) ? 1 : -1;

    // 3. tDelta = how far along ray (in t) to cross one full voxel on each axis
    glm::vec3 tDelta;
    tDelta.x = (direction.x != 0.0f) ? std::abs(1.0f / direction.x) : FLT_MAX;
    tDelta.y = (direction.y != 0.0f) ? std::abs(1.0f / direction.y) : FLT_MAX;
    tDelta.z = (direction.z != 0.0f) ? std::abs(1.0f / direction.z) : FLT_MAX;

    // 4. tMax = distance (in t) to next voxel boundary on each axis
    glm::vec3 tMax;
    tMax.x = ((step.x > 0) ? (std::floor(origin.x) + 1.0f - origin.x)
                            : (origin.x - std::floor(origin.x))) *
             tDelta.x;
    tMax.y = ((step.y > 0) ? (std::floor(origin.y) + 1.0f - origin.y)
                            : (origin.y - std::floor(origin.y))) *
             tDelta.y;
    tMax.z = ((step.z > 0) ? (std::floor(origin.z) + 1.0f - origin.z)
                            : (origin.z - std::floor(origin.z))) *
             tDelta.z;

    // 5. Track which face was last entered (initially none — first block is origin block)
    BlockFace face = BlockFace::PosY; // placeholder until first step

    // 6. Traverse — check current voxel first, then step
    float distance = 0.0f;
    while (distance <= maxDistance)
    {
        // Y-bounds check: world is [0 .. COLUMN_HEIGHT-1]
        if (blockPos.y < 0 || blockPos.y >= world::ChunkColumn::COLUMN_HEIGHT)
        {
            break;
        }

        // Check block at current position
        uint16_t blockId = world.getBlock(blockPos);
        if (blockId != world::BLOCK_AIR)
        {
            const auto& def = registry.getBlockType(blockId);
            if (def.hasCollision)
            {
                // Check for custom selection shapes
                if (shapeCache)
                {
                    auto shapes = shapeCache->getSelectionShape(blockPos, blockId);
                    if (!shapes.empty())
                    {
                        // Test ray against each custom selection box
                        glm::vec3 worldOffset{
                            static_cast<float>(blockPos.x),
                            static_cast<float>(blockPos.y),
                            static_cast<float>(blockPos.z)};

                        float closestT = FLT_MAX;
                        bool anyHit = false;

                        for (const auto& localBox : shapes)
                        {
                            math::AABB worldBox{
                                localBox.min + worldOffset,
                                localBox.max + worldOffset};

                            float t = 0.0f;
                            if (rayIntersectsAABB(origin, invDir, worldBox, t) && t < closestT)
                            {
                                closestT = t;
                                anyHit = true;
                            }
                        }

                        if (anyHit)
                        {
                            return RaycastResult{true, blockPos, prevPos, face, distance};
                        }
                        // Custom shape defined but ray missed all boxes — continue DDA
                    }
                    else
                    {
                        // No custom shape — use default full-block hit
                        return RaycastResult{true, blockPos, prevPos, face, distance};
                    }
                }
                else
                {
                    // No shape cache — default behavior
                    return RaycastResult{true, blockPos, prevPos, face, distance};
                }
            }
        }

        prevPos = blockPos;

        // Step along axis with smallest tMax
        if (tMax.x < tMax.y && tMax.x < tMax.z)
        {
            distance = tMax.x;
            tMax.x += tDelta.x;
            blockPos.x += step.x;
            face = (step.x > 0) ? BlockFace::NegX : BlockFace::PosX;
        }
        else if (tMax.y < tMax.z)
        {
            distance = tMax.y;
            tMax.y += tDelta.y;
            blockPos.y += step.y;
            face = (step.y > 0) ? BlockFace::NegY : BlockFace::PosY;
        }
        else
        {
            distance = tMax.z;
            tMax.z += tDelta.z;
            blockPos.z += step.z;
            face = (step.z > 0) ? BlockFace::NegZ : BlockFace::PosZ;
        }
    }

    return RaycastResult{false, {}, {}, BlockFace::PosY, 0.0f};
}

} // namespace voxel::physics
