#pragma once

#include "voxel/game/MiningState.h"
#include "voxel/math/AABB.h"
#include "voxel/math/MathTypes.h"
#include "voxel/physics/Raycast.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace voxel::world
{
class ChunkManager;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::scripting
{
class ShapeCache;
} // namespace voxel::scripting

namespace voxel::game
{

/// Movement state passed to tickPhysics() — decouples physics from InputManager.
struct MovementInput
{
    glm::vec3 wishDir{0.0f}; ///< Normalized horizontal movement direction.
    bool jump = false;
    bool sprint = false;
    bool sneak = false;
};

/// Tracks a sustained multi-phase block interaction (hold RMB on interactive block).
struct InteractionState
{
    bool isActive = false;
    glm::ivec3 targetBlockPos{0};
    uint16_t targetBlockId = 0;
    float elapsedTime = 0.0f;

    void reset()
    {
        isActive = false;
        targetBlockPos = glm::ivec3{0};
        targetBlockId = 0;
        elapsedTime = 0.0f;
    }
};

/**
 * @brief Handles player physics: gravity, swept AABB collision, step-up, ground detection,
 *        jumping, sprinting, sneaking, climbable blocks, move resistance, and damage blocks.
 *
 * Owns player position (dvec3 for world-scale precision), velocity (vec3 for per-tick deltas),
 * and AABB half-extents. The position represents the center-bottom (feet) of the player AABB.
 */
class PlayerController
{
public:
    static constexpr math::Vec3 HALF_EXTENTS{0.3f, 0.9f, 0.3f};
    static constexpr float EYE_HEIGHT = 1.62f;
    static constexpr float GRAVITY = 28.0f;
    static constexpr float TERMINAL_VELOCITY = 78.4f;
    static constexpr float WALK_SPEED = 4.317f;
    static constexpr float SPRINT_SPEED = 5.612f;
    static constexpr float SNEAK_SPEED = 1.295f;
    static constexpr float JUMP_VELOCITY = 8.0f;
    static constexpr float AIR_CONTROL = 0.02f;
    static constexpr float COLLISION_EPSILON = 0.001f;

    /**
     * @brief Initialize player at spawn position, ensuring the player is not inside a solid block.
     */
    void init(const glm::dvec3& spawnPos, world::ChunkManager& world, const world::BlockRegistry& registry);

    /**
     * @brief Per-tick physics update (testable, no input dependency).
     * @param dt Time delta in seconds.
     * @param input Movement state for this tick.
     */
    void tickPhysics(
        float dt,
        const MovementInput& input,
        world::ChunkManager& world,
        const world::BlockRegistry& registry);

    /**
     * @brief Update mining progress for one simulation tick.
     * @param dt Time delta in seconds.
     * @param result Current frame's raycast result.
     * @param lmbDown Whether the left mouse button is held.
     * @param world ChunkManager for block lookups.
     * @param registry BlockRegistry for hardness lookups.
     * @return true if mining just completed (progress >= 1.0) this tick.
     */
    bool updateMining(
        float dt,
        const physics::RaycastResult& result,
        bool lmbDown,
        const world::ChunkManager& world,
        const world::BlockRegistry& registry);

    /// Reset mining state to idle.
    void resetMining() { m_miningState.reset(); }

    [[nodiscard]] const MiningState& getMiningState() const { return m_miningState; }

    // --- Sustained interaction ---
    void startInteraction(const glm::ivec3& pos, uint16_t blockId);
    void updateInteraction(float dt);
    void stopInteraction();
    void cancelInteraction();
    [[nodiscard]] bool isInteracting() const { return m_interactionState.isActive; }
    [[nodiscard]] const InteractionState& getInteractionState() const { return m_interactionState; }

    [[nodiscard]] glm::dvec3 getPosition() const { return m_position; }
    [[nodiscard]] glm::dvec3 getEyePosition() const;
    [[nodiscard]] glm::vec3 getVelocity() const { return m_velocity; }
    [[nodiscard]] bool isOnGround() const { return m_isOnGround; }
    [[nodiscard]] bool isSprinting() const { return m_isSprinting; }
    [[nodiscard]] bool isSneaking() const { return m_isSneaking; }
    [[nodiscard]] bool isInClimbable() const { return m_isInClimbable; }
    [[nodiscard]] uint8_t getMaxResistance() const { return m_maxResistance; }
    void setPosition(const glm::dvec3& pos) { m_position = pos; }
    void setVelocity(const glm::vec3& vel) { m_velocity = vel; }
    void setShapeCache(scripting::ShapeCache* cache) { m_shapeCache = cache; }

    // --- Fall tracking ---
    [[nodiscard]] float consumeFallDistance();
    [[nodiscard]] bool justLanded() const { return m_justLanded; }

    [[nodiscard]] math::AABB getAABB() const;

    // --- Entity-block collision/overlap data (consumed by GameApp for Lua dispatch) ---

    /// A collision event recorded when the player AABB is clipped against a block.
    struct EntityBlockCollision
    {
        glm::ivec3 blockPos{0};
        uint16_t blockId = 0;
        std::string face;
        glm::vec3 velocity{0.0f};
        bool isImpact = false;
    };

    /// An overlap event recorded for each non-air block inside the player AABB.
    struct EntityBlockOverlap
    {
        glm::ivec3 blockPos{0};
        uint16_t blockId = 0;
    };

    [[nodiscard]] const std::vector<EntityBlockCollision>& getFrameCollisions() const { return m_frameCollisions; }
    [[nodiscard]] const std::vector<EntityBlockOverlap>& getFrameOverlaps() const { return m_frameOverlaps; }

private:
    glm::dvec3 m_position{0.0, 80.0, 0.0};
    glm::vec3 m_velocity{0.0f};
    bool m_isOnGround = false;
    bool m_isSprinting = false;
    bool m_isSneaking = false;
    bool m_isInClimbable = false;
    uint8_t m_maxResistance = 0;
    float m_damageAccumulator = 0.0f;
    float m_fallDistance = 0.0f;
    bool m_wasOnGround = false;
    bool m_justLanded = false;
    MiningState m_miningState;
    InteractionState m_interactionState;
    scripting::ShapeCache* m_shapeCache = nullptr;

    // Per-frame collision/overlap data for entity callbacks
    std::vector<EntityBlockCollision> m_frameCollisions;
    std::vector<EntityBlockOverlap> m_frameOverlaps;

    // Track previous-tick collision positions for isImpact detection
    std::vector<glm::ivec3> m_prevCollisionPositions;

    void scanOverlappingBlocks(float dt, world::ChunkManager& world, const world::BlockRegistry& registry);
    void applyGravity(float dt);
    void clampToEdge(
        const glm::vec3& proposedDelta,
        world::ChunkManager& world,
        const world::BlockRegistry& registry);
    void resolveCollisions(float dt, world::ChunkManager& world, const world::BlockRegistry& registry);
    void resolveAxis(int axis, float delta, world::ChunkManager& world, const world::BlockRegistry& registry);
    bool tryStepUp(int axis, float delta, world::ChunkManager& world, const world::BlockRegistry& registry);
    void ensureNotInsideBlock(world::ChunkManager& world, const world::BlockRegistry& registry);
};

} // namespace voxel::game
