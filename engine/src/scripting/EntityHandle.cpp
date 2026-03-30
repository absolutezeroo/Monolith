#include "voxel/scripting/EntityHandle.h"

#include "voxel/core/Log.h"
#include "voxel/game/PlayerController.h"

namespace voxel::scripting
{

EntityHandle::EntityHandle(game::PlayerController& player)
    : m_player(player)
{
}

void EntityHandle::damage(float amount)
{
    // V1: log only — no health system yet
    VX_LOG_INFO("Entity damage: {} HP", amount);
}

glm::vec3 EntityHandle::getVelocity() const
{
    return m_player.getVelocity();
}

glm::dvec3 EntityHandle::getPosition() const
{
    return m_player.getPosition();
}

void EntityHandle::setVelocity(const glm::vec3& vel)
{
    m_player.setVelocity(vel);
}

} // namespace voxel::scripting
