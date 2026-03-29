#pragma once

#include "voxel/math/AABB.h"
#include "voxel/math/MathTypes.h"

#include <glm/vec3.hpp>

namespace voxel::input
{
class InputManager;
}

namespace voxel::renderer
{
class Camera;
}

namespace voxel::world
{
class ChunkManager;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::game
{

/**
 * @brief Handles player physics: gravity, swept AABB collision, step-up, and ground detection.
 *
 * Owns player position (dvec3 for world-scale precision), velocity (vec3 for per-tick deltas),
 * and AABB half-extents. The position represents the center-bottom (feet) of the player AABB.
 *
 * This class reads input directly for basic WASD movement. Story 7.3 will layer
 * the command pipeline on top.
 */
class PlayerController
{
public:
    static constexpr math::Vec3 HALF_EXTENTS{0.3f, 0.9f, 0.3f};
    static constexpr float EYE_HEIGHT = 1.62f;
    static constexpr float GRAVITY = 28.0f;
    static constexpr float TERMINAL_VELOCITY = 78.4f;
    static constexpr float WALK_SPEED = 4.317f;
    static constexpr float COLLISION_EPSILON = 0.001f;

    /**
     * @brief Initialize player at spawn position, ensuring the player is not inside a solid block.
     */
    void init(const glm::dvec3& spawnPos, world::ChunkManager& world, const world::BlockRegistry& registry);

    /**
     * @brief Per-tick update: reads WASD input, applies gravity, resolves collisions.
     */
    void update(
        float dt,
        const input::InputManager& input,
        const renderer::Camera& camera,
        world::ChunkManager& world,
        const world::BlockRegistry& registry);

    /**
     * @brief Per-tick physics update without input dependency (testable).
     * @param dt Time delta in seconds.
     * @param wishDir Normalized horizontal movement direction (XZ plane). Zero = no movement.
     */
    void tickPhysics(
        float dt,
        const glm::vec3& wishDir,
        world::ChunkManager& world,
        const world::BlockRegistry& registry);

    [[nodiscard]] glm::dvec3 getPosition() const { return m_position; }
    [[nodiscard]] glm::dvec3 getEyePosition() const;
    [[nodiscard]] glm::vec3 getVelocity() const { return m_velocity; }
    [[nodiscard]] bool isOnGround() const { return m_isOnGround; }
    void setPosition(const glm::dvec3& pos) { m_position = pos; }

    [[nodiscard]] math::AABB getAABB() const;

private:
    glm::dvec3 m_position{0.0, 80.0, 0.0};
    glm::vec3 m_velocity{0.0f};
    bool m_isOnGround = false;

    void applyGravity(float dt);
    void resolveCollisions(float dt, world::ChunkManager& world, const world::BlockRegistry& registry);
    void resolveAxis(int axis, float delta, world::ChunkManager& world, const world::BlockRegistry& registry);
    bool tryStepUp(int axis, float delta, world::ChunkManager& world, const world::BlockRegistry& registry);
    void ensureNotInsideBlock(world::ChunkManager& world, const world::BlockRegistry& registry);
};

} // namespace voxel::game
