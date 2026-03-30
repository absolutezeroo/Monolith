#include "voxel/game/PlayerController.h"

#include "voxel/core/Log.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace voxel::game
{

static constexpr int WORLD_MIN_Y = 0;
static constexpr int WORLD_MAX_Y = 255;

void PlayerController::init(
    const glm::dvec3& spawnPos, world::ChunkManager& world, const world::BlockRegistry& registry)
{
    m_position = spawnPos;
    m_velocity = glm::vec3{0.0f};
    m_isOnGround = false;
    m_isSprinting = false;
    m_isSneaking = false;
    m_isInClimbable = false;
    m_maxResistance = 0;
    m_damageAccumulator = 0.0f;
    m_miningState.reset();
    ensureNotInsideBlock(world, registry);
}

bool PlayerController::updateMining(
    float dt,
    const physics::RaycastResult& result,
    bool lmbDown,
    const world::ChunkManager& world,
    const world::BlockRegistry& registry)
{
    // Reset conditions: LMB released or no hit
    if (!lmbDown || !result.hit)
    {
        m_miningState.reset();
        return false;
    }

    // Check distance from player to target (> 6 blocks cancels mining)
    auto eyePos = glm::vec3(getEyePosition());
    auto targetCenter = glm::vec3(result.blockPos) + glm::vec3(0.5f);
    float dist = glm::length(targetCenter - eyePos);
    if (dist > physics::MAX_REACH)
    {
        m_miningState.reset();
        return false;
    }

    // Target block changed — reset progress
    if (m_miningState.isMining && m_miningState.targetBlock != result.blockPos)
    {
        m_miningState.reset();
    }

    // Get block definition at target
    uint16_t blockId = world.getBlock(result.blockPos);
    if (blockId == world::BLOCK_AIR)
    {
        m_miningState.reset();
        return false;
    }
    const auto& def = registry.getBlockType(blockId);

    // Start mining a new block
    if (!m_miningState.isMining)
    {
        m_miningState.targetBlock = result.blockPos;
        m_miningState.isMining = true;
        m_miningState.progress = 0.0f;
        m_miningState.breakTime = calculateBreakTime(def);
    }

    // Accumulate progress
    if (m_miningState.breakTime > 0.0f)
    {
        m_miningState.progress += dt / m_miningState.breakTime;
        m_miningState.progress = std::min(m_miningState.progress, 1.0f);
    }
    else
    {
        m_miningState.progress = 1.0f;
    }

    // Update crack stage: floor(progress * 10), clamped 0-9
    m_miningState.crackStage = std::clamp(
        static_cast<int>(std::floor(m_miningState.progress * static_cast<float>(MAX_CRACK_STAGES))),
        0,
        MAX_CRACK_STAGES - 1);

    // Check if mining completed
    if (m_miningState.progress >= 1.0f)
    {
        m_miningState.reset();
        return true;
    }

    return false;
}

void PlayerController::scanOverlappingBlocks(
    float dt, world::ChunkManager& world, const world::BlockRegistry& registry)
{
    m_isInClimbable = false;
    m_maxResistance = 0;
    uint32_t frameDamage = 0;

    math::AABB playerBox = getAABB();
    glm::ivec3 minBlock = glm::ivec3(glm::floor(glm::vec3(playerBox.min)));
    glm::ivec3 maxBlock = glm::ivec3(glm::floor(glm::vec3(
        playerBox.max.x - 0.001f, playerBox.max.y - 0.001f, playerBox.max.z - 0.001f)));

    // Clamp to world Y bounds to avoid out-of-bounds getBlock calls
    minBlock.y = std::max(minBlock.y, WORLD_MIN_Y);
    maxBlock.y = std::min(maxBlock.y, WORLD_MAX_Y);

    for (int y = minBlock.y; y <= maxBlock.y; ++y)
    {
        for (int x = minBlock.x; x <= maxBlock.x; ++x)
        {
            for (int z = minBlock.z; z <= maxBlock.z; ++z)
            {
                uint16_t stateId = world.getBlock({x, y, z});
                if (stateId == world::BLOCK_AIR)
                {
                    continue;
                }
                const auto& def = registry.getBlockType(stateId);

                if (def.isClimbable)
                {
                    m_isInClimbable = true;
                }
                m_maxResistance = std::max(m_maxResistance, def.moveResistance);
                frameDamage += def.damagePerSecond;
            }
        }
    }

    // Accumulate damage over ticks (20 ticks = 1 second)
    if (frameDamage > 0)
    {
        m_damageAccumulator += dt;
        if (m_damageAccumulator >= 1.0f)
        {
            if (core::Log::getLogger())
            {
                VX_LOG_INFO("Player takes {} damage", frameDamage);
            }
            m_damageAccumulator -= 1.0f;
        }
    }
    else
    {
        m_damageAccumulator = 0.0f;
    }
}

void PlayerController::tickPhysics(
    float dt,
    const MovementInput& input,
    world::ChunkManager& world,
    const world::BlockRegistry& registry)
{
    // 1. Scan overlapping blocks for physics properties
    scanOverlappingBlocks(dt, world, registry);

    // 2. Update sprint/sneak state (mutually exclusive)
    m_isSneaking = input.sneak;
    m_isSprinting = input.sprint;
    if (m_isSneaking)
    {
        m_isSprinting = false;
    }

    // 3. Resistance multiplier
    float resistanceMul = 1.0f / (1.0f + static_cast<float>(m_maxResistance));

    if (m_isInClimbable)
    {
        // Climbable block behavior: disable gravity, controlled vertical movement
        float horizSpeed = WALK_SPEED * resistanceMul;
        float vertSpeed = SNEAK_SPEED * resistanceMul;

        bool hasHorizInput = glm::length(input.wishDir) > 0.001f;
        bool wantsUp = input.jump;
        bool wantsDown = input.sneak;

        if (hasHorizInput)
        {
            m_velocity.x = input.wishDir.x * horizSpeed;
            m_velocity.z = input.wishDir.z * horizSpeed;
        }
        else
        {
            m_velocity.x = 0.0f;
            m_velocity.z = 0.0f;
        }

        if (wantsUp)
        {
            m_velocity.y = vertSpeed;
        }
        else if (wantsDown)
        {
            m_velocity.y = -vertSpeed;
        }
        else
        {
            m_velocity.y = 0.0f;
        }
    }
    else
    {
        // Normal movement
        // Determine effective speed
        float effectiveSpeed = WALK_SPEED;
        if (m_isSprinting)
        {
            effectiveSpeed = SPRINT_SPEED;
        }
        else if (m_isSneaking)
        {
            effectiveSpeed = SNEAK_SPEED;
        }

        effectiveSpeed *= resistanceMul;

        // Air control: reduce horizontal acceleration while airborne
        if (m_isOnGround)
        {
            m_velocity.x = input.wishDir.x * effectiveSpeed;
            m_velocity.z = input.wishDir.z * effectiveSpeed;
        }
        else
        {
            // Blend toward wish velocity with AIR_CONTROL factor
            float wishX = input.wishDir.x * effectiveSpeed;
            float wishZ = input.wishDir.z * effectiveSpeed;
            m_velocity.x += (wishX - m_velocity.x) * AIR_CONTROL;
            m_velocity.z += (wishZ - m_velocity.z) * AIR_CONTROL;
        }

        // Jump: only when on ground
        if (input.jump && m_isOnGround)
        {
            m_velocity.y = JUMP_VELOCITY;
            m_isOnGround = false;
        }

        // Apply gravity (with resistance reduction)
        applyGravity(dt * resistanceMul);
    }

    // Sneak edge detection: before collision resolution, clamp horizontal movement
    if (m_isSneaking && m_isOnGround && !m_isInClimbable)
    {
        glm::vec3 proposedDelta{m_velocity.x * dt, 0.0f, m_velocity.z * dt};
        clampToEdge(proposedDelta, world, registry);
    }

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

void PlayerController::clampToEdge(
    const glm::vec3& proposedDelta,
    world::ChunkManager& world,
    const world::BlockRegistry& registry)
{
    float halfW = HALF_EXTENTS.x; // 0.3

    // Helper: check if a foot corner position has solid ground below it
    auto hasGroundBelow = [&](double cx, double cy, double cz) -> bool {
        glm::ivec3 belowBlock = glm::ivec3(
            static_cast<int>(std::floor(cx)),
            static_cast<int>(std::floor(cy - 1.0)),
            static_cast<int>(std::floor(cz)));
        if (belowBlock.y < WORLD_MIN_Y || belowBlock.y > WORLD_MAX_Y)
        {
            return false;
        }
        uint16_t stateId = world.getBlock(belowBlock);
        if (stateId == world::BLOCK_AIR)
        {
            return false;
        }
        const auto& def = registry.getBlockType(stateId);
        return def.hasCollision;
    };

    // Minecraft-style: block movement on an axis only if at the new position
    // ZERO corners have ground below. As long as at least one corner is still
    // over solid ground, the player can overhang the edge.
    for (int axis : {0, 2})
    {
        if (std::abs(proposedDelta[axis]) < 1e-8f)
        {
            continue;
        }

        glm::dvec3 newPos = m_position;
        newPos[axis] += static_cast<double>(proposedDelta[axis]);

        double corners[4][2] = {
            {newPos.x - halfW, newPos.z - halfW},
            {newPos.x + halfW, newPos.z - halfW},
            {newPos.x - halfW, newPos.z + halfW},
            {newPos.x + halfW, newPos.z + halfW},
        };

        bool anyCornerHasGround = false;
        for (const auto& corner : corners)
        {
            if (hasGroundBelow(corner[0], newPos.y, corner[1]))
            {
                anyCornerHasGround = true;
                break;
            }
        }

        if (!anyCornerHasGround)
        {
            m_velocity[axis] = 0.0f;
        }
    }
}

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

    // Compute the range of blocks the player's leading edge overlaps.
    // The player footprint can straddle two blocks on the perpendicular axis.
    int leadingEdge = 0;
    int perpMin = 0;
    int perpMax = 0;

    if (axis == 0)
    {
        leadingEdge = delta > 0.0f ? static_cast<int>(std::floor(testPosF.x + HALF_EXTENTS.x))
                                   : static_cast<int>(std::floor(testPosF.x - HALF_EXTENTS.x));
        perpMin = static_cast<int>(std::floor(pos.z - HALF_EXTENTS.z));
        perpMax = static_cast<int>(std::floor(pos.z + HALF_EXTENTS.z));
    }
    else
    {
        leadingEdge = delta > 0.0f ? static_cast<int>(std::floor(testPosF.z + HALF_EXTENTS.z))
                                   : static_cast<int>(std::floor(testPosF.z - HALF_EXTENTS.z));
        perpMin = static_cast<int>(std::floor(pos.x - HALF_EXTENTS.x));
        perpMax = static_cast<int>(std::floor(pos.x + HALF_EXTENTS.x));
    }

    int stepTopY = feetY + 1;
    if (stepTopY > WORLD_MAX_Y)
    {
        return false;
    }

    // Check all blocks at the leading edge for a feet-level obstacle
    bool foundObstacle = false;
    for (int p = perpMin; p <= perpMax; ++p)
    {
        int bx = (axis == 0) ? leadingEdge : p;
        int bz = (axis == 0) ? p : leadingEdge;

        uint16_t feetBlock = world.getBlock(glm::ivec3{bx, feetY, bz});
        if (feetBlock != world::BLOCK_AIR && registry.getBlockType(feetBlock).hasCollision)
        {
            foundObstacle = true;

            // If the block above is also solid, obstacle is 2+ blocks tall — can't step
            uint16_t aboveBlock = world.getBlock(glm::ivec3{bx, stepTopY, bz});
            if (aboveBlock != world::BLOCK_AIR && registry.getBlockType(aboveBlock).hasCollision)
            {
                return false;
            }
        }
    }

    if (!foundObstacle)
    {
        return false; // No obstacle at feet level
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

    // Check all blocks the player overlaps vertically (height = 1.8 blocks).
    // With fractional Y the player can span 3 blocks instead of 2.
    auto isPositionClear = [&](double feetY) -> bool {
        int minY = static_cast<int>(std::floor(feetY));
        int maxY = static_cast<int>(std::floor(feetY + static_cast<double>(HALF_EXTENTS.y * 2.0f) - 0.001));
        if (minY < WORLD_MIN_Y || maxY > WORLD_MAX_Y)
        {
            return false;
        }
        for (int y = minY; y <= maxY; ++y)
        {
            uint16_t blockId = world.getBlock(glm::ivec3{centerX, y, centerZ});
            if (blockId != world::BLOCK_AIR && registry.getBlockType(blockId).hasCollision)
            {
                return false;
            }
        }
        return true;
    };

    if (isPositionClear(m_position.y))
    {
        return; // Already in a valid position
    }

    // Scan upward to find 2 consecutive clear blocks
    int maxScanY = std::min(startY + 256, static_cast<int>(world::ChunkColumn::COLUMN_HEIGHT) - 2);
    for (int y = startY + 1; y <= maxScanY; ++y)
    {
        if (isPositionClear(static_cast<double>(y)))
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

void PlayerController::startInteraction(const glm::ivec3& pos, uint16_t blockId)
{
    m_interactionState.isActive = true;
    m_interactionState.targetBlockPos = pos;
    m_interactionState.targetBlockId = blockId;
    m_interactionState.elapsedTime = 0.0f;
}

void PlayerController::updateInteraction(float dt)
{
    if (m_interactionState.isActive)
    {
        m_interactionState.elapsedTime += dt;
    }
}

void PlayerController::stopInteraction()
{
    m_interactionState.reset();
}

void PlayerController::cancelInteraction()
{
    m_interactionState.reset();
}

} // namespace voxel::game
