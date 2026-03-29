#include "voxel/physics/Raycast.h"

#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"

#include <glm/common.hpp>

#include <cfloat>
#include <cmath>

namespace voxel::physics
{

RaycastResult raycast(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    const world::ChunkManager& world,
    const world::BlockRegistry& registry)
{
    using renderer::BlockFace;

    // 1. Starting voxel = floor(origin)
    // NOTE: prevPos starts equal to blockPos. If origin is inside a solid block,
    // the result will have previousPos == blockPos (distance ≈ 0). Story 7.5
    // must handle this edge case when using previousPos for block placement.
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
                return RaycastResult{true, blockPos, prevPos, face, distance};
            }
        }

        prevPos = blockPos;

        // Step along axis with smallest tMax
        // Face entered = OPPOSITE of step direction (we stepped +X -> entered NegX face)
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
