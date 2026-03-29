#pragma once

#include "voxel/renderer/ChunkMesh.h"

#include <glm/vec3.hpp>

#include <cstdint>

namespace voxel::world
{
class ChunkManager;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::physics
{

struct RaycastResult
{
    bool hit = false;
    glm::ivec3 blockPos{0};    // Position of the hit block
    glm::ivec3 previousPos{0}; // Air block before hit (placement target for Story 7.5)
    renderer::BlockFace face = renderer::BlockFace::PosY; // Face the ray entered
    float distance = 0.0f;     // Distance from origin to hit
};

/// Maximum block targeting reach (configurable — used in GameApp).
static constexpr float MAX_REACH = 6.0f;

/// Cast a ray through the voxel grid using Amanatides & Woo DDA.
/// @param origin      Ray origin (world space, e.g., camera eye position).
/// @param direction   Normalized ray direction.
/// @param maxDistance  Maximum traversal distance in blocks.
/// @param world       ChunkManager for block queries.
/// @param registry    BlockRegistry for collision checks.
/// @return RaycastResult with hit=true if a solid block was found.
RaycastResult raycast(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    const world::ChunkManager& world,
    const world::BlockRegistry& registry);

} // namespace voxel::physics
