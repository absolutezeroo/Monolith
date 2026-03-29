#include "voxel/game/PlayerController.h"

#include "voxel/core/Log.h"
#include "voxel/input/InputManager.h"
#include "voxel/renderer/Camera.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"

#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace voxel::game
{

void PlayerController::init(
    const glm::dvec3& spawnPos, world::ChunkManager& world, const world::BlockRegistry& registry)
{
    m_position = spawnPos;
    m_velocity = glm::vec3{0.0f};
    m_isOnGround = false;
    ensureNotInsideBlock(world, registry);
}

void PlayerController::update(
    float dt,
    const input::InputManager& input,
    const renderer::Camera& camera,
    world::ChunkManager& world,
    const world::BlockRegistry& registry)
{
    // Horizontal movement from WASD input
    glm::vec3 forward = camera.getForward();
    glm::vec3 right = camera.getRight();

    // Project onto XZ plane for ground movement
    forward.y = 0.0f;
    right.y = 0.0f;
    if (glm::length(forward) > 0.0f)
    {
        forward = glm::normalize(forward);
    }
    if (glm::length(right) > 0.0f)
    {
        right = glm::normalize(right);
    }

    glm::vec3 moveDir{0.0f};
    if (input.isKeyDown(GLFW_KEY_W))
    {
        moveDir += forward;
    }
    if (input.isKeyDown(GLFW_KEY_S))
    {
        moveDir -= forward;
    }
    if (input.isKeyDown(GLFW_KEY_D))
    {
        moveDir += right;
    }
    if (input.isKeyDown(GLFW_KEY_A))
    {
        moveDir -= right;
    }

    if (glm::length(moveDir) > 0.0f)
    {
        moveDir = glm::normalize(moveDir);
    }

    tickPhysics(dt, moveDir, world, registry);
}

void PlayerController::tickPhysics(
    float dt,
    const glm::vec3& wishDir,
    world::ChunkManager& world,
    const world::BlockRegistry& registry)
{
    // Set horizontal velocity from wish direction
    m_velocity.x = wishDir.x * WALK_SPEED;
    m_velocity.z = wishDir.z * WALK_SPEED;

    // Apply gravity
    applyGravity(dt);

    // Resolve collisions axis by axis: Y first, then X, then Z
    resolveCollisions(dt, world, registry);
}

glm::dvec3 PlayerController::getEyePosition() const
{
    return m_position + glm::dvec3{0.0, EYE_HEIGHT, 0.0};
}

math::AABB PlayerController::getAABB() const
{
    auto pos = glm::vec3(m_position);
    return math::AABB{
        math::Vec3{pos.x - HALF_EXTENTS.x, pos.y, pos.z - HALF_EXTENTS.z},
        math::Vec3{pos.x + HALF_EXTENTS.x, pos.y + HALF_EXTENTS.y * 2.0f, pos.z + HALF_EXTENTS.z}};
}

void PlayerController::applyGravity(float dt)
{
    m_velocity.y -= GRAVITY * dt;
    if (m_velocity.y < -TERMINAL_VELOCITY)
    {
        m_velocity.y = -TERMINAL_VELOCITY;
    }
}

static constexpr int WORLD_MIN_Y = 0;
static constexpr int WORLD_MAX_Y = 255;

/// Collect all solid block AABBs that overlap the given volume.
static std::vector<math::AABB> collectSolidBlocks(
    const math::AABB& volume, world::ChunkManager& world, const world::BlockRegistry& registry)
{
    std::vector<math::AABB> result;

    int minX = static_cast<int>(std::floor(volume.min.x));
    int minY = std::max(static_cast<int>(std::floor(volume.min.y)), WORLD_MIN_Y);
    int minZ = static_cast<int>(std::floor(volume.min.z));
    int maxX = static_cast<int>(std::floor(volume.max.x));
    int maxY = std::min(static_cast<int>(std::floor(volume.max.y)), WORLD_MAX_Y);
    int maxZ = static_cast<int>(std::floor(volume.max.z));

    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                uint16_t stateId = world.getBlock(glm::ivec3{x, y, z});
                if (stateId == world::BLOCK_AIR)
                {
                    continue;
                }
                const auto& def = registry.getBlockType(stateId);
                if (!def.hasCollision)
                {
                    continue;
                }
                result.push_back(math::AABB{
                    math::Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                    math::Vec3{static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)}});
            }
        }
    }

    return result;
}

void PlayerController::resolveCollisions(
    float dt, world::ChunkManager& world, const world::BlockRegistry& registry)
{
    // Y first (gravity), then X, then Z
    resolveAxis(1, m_velocity.y * dt, world, registry); // Y
    resolveAxis(0, m_velocity.x * dt, world, registry); // X
    resolveAxis(2, m_velocity.z * dt, world, registry); // Z
}

void PlayerController::resolveAxis(
    int axis, float delta, world::ChunkManager& world, const world::BlockRegistry& registry)
{
    if (std::abs(delta) < 1e-8f)
    {
        return;
    }

    // Reset OnGround at the start of Y resolution
    if (axis == 1)
    {
        m_isOnGround = false;
    }

    // Build the swept volume: current AABB expanded by the movement delta
    math::AABB playerBox = getAABB();
    math::AABB sweptVolume = playerBox;

    if (delta > 0.0f)
    {
        sweptVolume.max[axis] += delta;
    }
    else
    {
        sweptVolume.min[axis] += delta;
    }

    auto candidates = collectSolidBlocks(sweptVolume, world, registry);

    // Sort candidates by distance along movement direction (closest first)
    if (delta > 0.0f)
    {
        std::sort(candidates.begin(), candidates.end(), [axis](const math::AABB& a, const math::AABB& b) {
            return a.min[axis] < b.min[axis];
        });
    }
    else
    {
        std::sort(candidates.begin(), candidates.end(), [axis](const math::AABB& a, const math::AABB& b) {
            return a.max[axis] > b.max[axis];
        });
    }

    // Clip delta against each candidate
    float clippedDelta = delta;
    for (const auto& block : candidates)
    {
        // Check overlap on the other two axes
        int ax1 = (axis + 1) % 3;
        int ax2 = (axis + 2) % 3;

        if (playerBox.max[ax1] <= block.min[ax1] || playerBox.min[ax1] >= block.max[ax1] ||
            playerBox.max[ax2] <= block.min[ax2] || playerBox.min[ax2] >= block.max[ax2])
        {
            continue; // No overlap on perpendicular axes
        }

        if (clippedDelta > 0.0f)
        {
            float maxSafe = block.min[axis] - playerBox.max[axis] - COLLISION_EPSILON;
            if (maxSafe < clippedDelta)
            {
                clippedDelta = std::max(maxSafe, 0.0f);
            }
        }
        else
        {
            float maxSafe = block.max[axis] - playerBox.min[axis] + COLLISION_EPSILON;
            if (maxSafe > clippedDelta)
            {
                clippedDelta = std::min(maxSafe, 0.0f);
            }
        }
    }

    // Apply clipped delta to position
    m_position[axis] += static_cast<double>(clippedDelta);

    // Check if movement was clipped
    bool wasClipped = std::abs(clippedDelta) < std::abs(delta) - 1e-6f;

    if (wasClipped)
    {
        if (axis == 1 && delta < 0.0f)
        {
            m_isOnGround = true;
        }
        m_velocity[axis] = 0.0f;

        // Try step-up on horizontal axes
        if (axis != 1)
        {
            // Undo the clipped delta so tryStepUp works from the pre-collision position
            glm::dvec3 clippedPos = m_position;
            m_position[axis] -= static_cast<double>(clippedDelta);

            if (!tryStepUp(axis, delta, world, registry))
            {
                // Step-up failed — restore the clipped position
                m_position = clippedPos;
            }
        }
    }
}

bool PlayerController::tryStepUp(
    int axis, float delta, world::ChunkManager& world, const world::BlockRegistry& registry)
{
    if (!m_isOnGround)
    {
        return false;
    }

    // Check if the blocking voxel is at feet level and has air above
    auto pos = glm::vec3(m_position);
    int feetY = static_cast<int>(std::floor(m_position.y));

    // Where would we be after the horizontal move?
    glm::dvec3 testPos = m_position;
    testPos[axis] += static_cast<double>(delta);
    auto testPosF = glm::vec3(testPos);

    // Compute the block position we'd collide with
    int blockX = 0;
    int blockZ = 0;

    if (axis == 0)
    {
        blockX = delta > 0.0f ? static_cast<int>(std::floor(testPosF.x + HALF_EXTENTS.x))
                              : static_cast<int>(std::floor(testPosF.x - HALF_EXTENTS.x));
        blockZ = static_cast<int>(std::floor(pos.z));
    }
    else
    {
        blockX = static_cast<int>(std::floor(pos.x));
        blockZ = delta > 0.0f ? static_cast<int>(std::floor(testPosF.z + HALF_EXTENTS.z))
                              : static_cast<int>(std::floor(testPosF.z - HALF_EXTENTS.z));
    }

    // Is there a solid block at feet level?
    uint16_t feetBlock = world.getBlock(glm::ivec3{blockX, feetY, blockZ});
    if (feetBlock == world::BLOCK_AIR || !registry.getBlockType(feetBlock).hasCollision)
    {
        return false; // No obstacle at feet level
    }

    // Is the block above the obstacle clear?
    int stepTopY = feetY + 1;
    if (stepTopY > WORLD_MAX_Y)
    {
        return false;
    }
    uint16_t aboveBlock = world.getBlock(glm::ivec3{blockX, stepTopY, blockZ});
    if (aboveBlock != world::BLOCK_AIR && registry.getBlockType(aboveBlock).hasCollision)
    {
        return false; // Obstacle above the step
    }

    // Check headroom: player is 1.8 blocks tall at the stepped-up position
    float steppedY = static_cast<float>(stepTopY);
    float playerTopY = steppedY + HALF_EXTENTS.y * 2.0f;
    int headCheckY = std::min(static_cast<int>(std::floor(playerTopY)), WORLD_MAX_Y);
    for (int y = stepTopY; y <= headCheckY; ++y)
    {
        // Check in a wider area covering the player's AABB
        int minBX = static_cast<int>(std::floor(testPosF.x - HALF_EXTENTS.x));
        int maxBX = static_cast<int>(std::floor(testPosF.x + HALF_EXTENTS.x));
        int minBZ = static_cast<int>(std::floor(testPosF.z - HALF_EXTENTS.z));
        int maxBZ = static_cast<int>(std::floor(testPosF.z + HALF_EXTENTS.z));

        for (int bx = minBX; bx <= maxBX; ++bx)
        {
            for (int bz = minBZ; bz <= maxBZ; ++bz)
            {
                uint16_t blockId = world.getBlock(glm::ivec3{bx, y, bz});
                if (blockId != world::BLOCK_AIR && registry.getBlockType(blockId).hasCollision)
                {
                    return false; // No headroom
                }
            }
        }
    }

    // Step up: move Y to top of the step block
    m_position.y = static_cast<double>(stepTopY);

    // Re-apply horizontal movement
    m_position[axis] += static_cast<double>(delta);

    return true;
}

void PlayerController::ensureNotInsideBlock(world::ChunkManager& world, const world::BlockRegistry& registry)
{
    // Scan upward for 2 consecutive air blocks starting from current position
    int startY = static_cast<int>(std::floor(m_position.y));
    int centerX = static_cast<int>(std::floor(m_position.x));
    int centerZ = static_cast<int>(std::floor(m_position.z));

    // Check if current position is already valid
    auto isPositionClear = [&](int y) -> bool {
        if (y < WORLD_MIN_Y || y + 1 > WORLD_MAX_Y)
        {
            return false;
        }
        // Player occupies 2 blocks vertically from feet position
        for (int dy = 0; dy < 2; ++dy)
        {
            uint16_t blockId = world.getBlock(glm::ivec3{centerX, y + dy, centerZ});
            if (blockId != world::BLOCK_AIR && registry.getBlockType(blockId).hasCollision)
            {
                return false;
            }
        }
        return true;
    };

    if (isPositionClear(startY))
    {
        return; // Already in a valid position
    }

    // Scan upward to find 2 consecutive clear blocks
    int maxScanY = std::min(startY + 256, static_cast<int>(world::ChunkColumn::COLUMN_HEIGHT) - 2);
    for (int y = startY + 1; y <= maxScanY; ++y)
    {
        if (isPositionClear(y))
        {
            m_position.y = static_cast<double>(y);
            if (core::Log::getLogger())
            {
                VX_LOG_INFO("Spawn safety: pushed player from Y={} to Y={}", startY, y);
            }
            return;
        }
    }

    if (core::Log::getLogger())
    {
        VX_LOG_WARN("Spawn safety: could not find clear space above Y={}", startY);
    }
}

} // namespace voxel::game
