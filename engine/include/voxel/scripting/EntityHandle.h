#pragma once

#include <glm/vec3.hpp>

namespace voxel::game
{
class PlayerController;
} // namespace voxel::game

namespace voxel::scripting
{

/// Lightweight handle passed to entity-block callbacks in Lua.
/// V1: wraps PlayerController only. Future: wraps any ECS entity.
class EntityHandle
{
public:
    explicit EntityHandle(game::PlayerController& player);

    void damage(float amount);
    [[nodiscard]] glm::vec3 getVelocity() const;
    [[nodiscard]] glm::dvec3 getPosition() const;
    void setVelocity(const glm::vec3& vel);

private:
    game::PlayerController& m_player;
};

} // namespace voxel::scripting
