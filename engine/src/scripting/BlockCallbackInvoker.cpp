#include "voxel/scripting/BlockCallbackInvoker.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/EntityHandle.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"

#include <sol/sol.hpp>

namespace voxel::scripting
{

BlockCallbackInvoker::BlockCallbackInvoker(sol::state& lua, world::BlockRegistry& registry)
    : m_lua(lua), m_registry(registry)
{
}

static sol::table posToTable(sol::state& lua, const glm::ivec3& pos)
{
    return lua.create_table_with("x", pos.x, "y", pos.y, "z", pos.z);
}

// --- Placement callbacks ---

bool BlockCallbackInvoker::invokeCanPlace(const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->canPlace.has_value())
    {
        return true;
    }

    sol::protected_function_result result = (*def.callbacks->canPlace)(posToTable(m_lua, pos), playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua can_place error for '{}': {}", def.stringId, err.what());
        return true;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : true;
}

void BlockCallbackInvoker::invokeOnPlace(const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onPlace.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->onPlace)(posToTable(m_lua, pos), playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_place error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeOnConstruct(const world::BlockDefinition& def, const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->onConstruct.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->onConstruct)(posToTable(m_lua, pos));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_construct error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeAfterPlace(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->afterPlace.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->afterPlace)(posToTable(m_lua, pos), playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua after_place error for '{}': {}", def.stringId, err.what());
    }
}

// --- Destruction callbacks ---

bool BlockCallbackInvoker::invokeCanDig(const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->canDig.has_value())
    {
        return true;
    }

    sol::protected_function_result result = (*def.callbacks->canDig)(posToTable(m_lua, pos), playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua can_dig error for '{}': {}", def.stringId, err.what());
        return true;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : true;
}

void BlockCallbackInvoker::invokeOnDestruct(const world::BlockDefinition& def, const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->onDestruct.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->onDestruct)(posToTable(m_lua, pos));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_destruct error for '{}': {}", def.stringId, err.what());
    }
}

bool BlockCallbackInvoker::invokeOnDig(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t blockId, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onDig.has_value())
    {
        return true;
    }

    sol::protected_function_result result = (*def.callbacks->onDig)(posToTable(m_lua, pos), blockId, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_dig error for '{}': {}", def.stringId, err.what());
        return true;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : true;
}

void BlockCallbackInvoker::invokeAfterDestruct(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t oldBlockId)
{
    if (!def.callbacks || !def.callbacks->afterDestruct.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->afterDestruct)(posToTable(m_lua, pos), oldBlockId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua after_destruct error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeAfterDig(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t oldBlockId, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->afterDig.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->afterDig)(posToTable(m_lua, pos), oldBlockId, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua after_dig error for '{}': {}", def.stringId, err.what());
    }
}

// --- Timer callbacks ---

bool BlockCallbackInvoker::invokeOnTimer(const world::BlockDefinition& def, const glm::ivec3& pos, float elapsed)
{
    if (!def.callbacks || !def.callbacks->onTimer.has_value())
    {
        return false;
    }

    sol::protected_function_result result = (*def.callbacks->onTimer)(posToTable(m_lua, pos), elapsed);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_timer error for '{}': {}", def.stringId, err.what());
        return false;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : false;
}

// --- ABM/LBM action callbacks ---

void BlockCallbackInvoker::invokeABMAction(
    const sol::protected_function& action,
    const glm::ivec3& pos,
    uint16_t blockId,
    int activeObjectCount)
{
    sol::protected_function_result result = action(posToTable(m_lua, pos), blockId, activeObjectCount);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua ABM action error at ({},{},{}): {}", pos.x, pos.y, pos.z, err.what());
    }
}

void BlockCallbackInvoker::invokeLBMAction(
    const sol::protected_function& action, const glm::ivec3& pos, uint16_t blockId, float dtimeS)
{
    sol::protected_function_result result = action(posToTable(m_lua, pos), blockId, dtimeS);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua LBM action error at ({},{},{}): {}", pos.x, pos.y, pos.z, err.what());
    }
}

// --- Interaction callbacks ---

void BlockCallbackInvoker::invokeOnRightclick(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t blockId, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onRightclick.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onRightclick)(posToTable(m_lua, pos), blockId, playerId, sol::nil, sol::nil);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_rightclick error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeOnPunch(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t blockId, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onPunch.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onPunch)(posToTable(m_lua, pos), blockId, playerId, sol::nil);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_punch error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeOnSecondaryUse(const world::BlockDefinition& def, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onSecondaryUse.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->onSecondaryUse)(sol::nil, playerId, sol::nil);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_secondary_use error for '{}': {}", def.stringId, err.what());
    }
}

bool BlockCallbackInvoker::invokeOnInteractStart(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onInteractStart.has_value())
    {
        return false;
    }

    sol::protected_function_result result = (*def.callbacks->onInteractStart)(posToTable(m_lua, pos), playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_interact_start error for '{}': {}", def.stringId, err.what());
        return false;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : false;
}

bool BlockCallbackInvoker::invokeOnInteractStep(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId, float elapsedSeconds)
{
    if (!def.callbacks || !def.callbacks->onInteractStep.has_value())
    {
        return false;
    }

    sol::protected_function_result result =
        (*def.callbacks->onInteractStep)(posToTable(m_lua, pos), playerId, elapsedSeconds);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_interact_step error for '{}': {}", def.stringId, err.what());
        return false;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : false;
}

void BlockCallbackInvoker::invokeOnInteractStop(
    const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId, float elapsedSeconds)
{
    if (!def.callbacks || !def.callbacks->onInteractStop.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onInteractStop)(posToTable(m_lua, pos), playerId, elapsedSeconds);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_interact_stop error for '{}': {}", def.stringId, err.what());
    }
}

bool BlockCallbackInvoker::invokeOnInteractCancel(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    uint32_t playerId,
    float elapsedSeconds,
    const std::string& reason)
{
    if (!def.callbacks || !def.callbacks->onInteractCancel.has_value())
    {
        return true;
    }

    sol::protected_function_result result =
        (*def.callbacks->onInteractCancel)(posToTable(m_lua, pos), playerId, elapsedSeconds, reason);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_interact_cancel error for '{}': {}", def.stringId, err.what());
        return true;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : true;
}

// --- Entity-block interaction callbacks ---

void BlockCallbackInvoker::invokeOnEntityInside(
    const world::BlockDefinition& def, const glm::ivec3& pos, EntityHandle& entity)
{
    if (!def.callbacks || !def.callbacks->onEntityInside.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onEntityInside)(posToTable(m_lua, pos), std::ref(entity));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_entity_inside error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeOnEntityStepOn(
    const world::BlockDefinition& def, const glm::ivec3& pos, EntityHandle& entity)
{
    if (!def.callbacks || !def.callbacks->onEntityStepOn.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onEntityStepOn)(posToTable(m_lua, pos), std::ref(entity));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_entity_step_on error for '{}': {}", def.stringId, err.what());
    }
}

float BlockCallbackInvoker::invokeOnEntityFallOn(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    EntityHandle& entity,
    float fallDistance)
{
    if (!def.callbacks || !def.callbacks->onEntityFallOn.has_value())
    {
        return 1.0f;
    }

    sol::protected_function_result result =
        (*def.callbacks->onEntityFallOn)(posToTable(m_lua, pos), std::ref(entity), fallDistance);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_entity_fall_on error for '{}': {}", def.stringId, err.what());
        return 1.0f;
    }

    return result.get_type() == sol::type::number ? result.get<float>() : 1.0f;
}

void BlockCallbackInvoker::invokeOnEntityCollide(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    EntityHandle& entity,
    const std::string& facing,
    const glm::vec3& velocity,
    bool isImpact)
{
    if (!def.callbacks || !def.callbacks->onEntityCollide.has_value())
    {
        return;
    }

    auto velTable = m_lua.create_table_with("x", velocity.x, "y", velocity.y, "z", velocity.z);
    sol::protected_function_result result =
        (*def.callbacks->onEntityCollide)(posToTable(m_lua, pos), std::ref(entity), facing, velTable, isImpact);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_entity_collide error for '{}': {}", def.stringId, err.what());
    }
}

// --- Inventory callbacks ---

int BlockCallbackInvoker::invokeAllowInventoryPut(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& listname,
    size_t index,
    const world::ItemStack& stack,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->allowInventoryPut.has_value())
    {
        return static_cast<int>(stack.count);
    }

    sol::protected_function_result result =
        (*def.callbacks->allowInventoryPut)(posToTable(m_lua, pos), listname, index, stack, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua allow_inventory_put error for '{}': {}", def.stringId, err.what());
        return static_cast<int>(stack.count);
    }

    return result.get_type() == sol::type::number ? result.get<int>() : static_cast<int>(stack.count);
}

int BlockCallbackInvoker::invokeAllowInventoryTake(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& listname,
    size_t index,
    const world::ItemStack& stack,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->allowInventoryTake.has_value())
    {
        return static_cast<int>(stack.count);
    }

    sol::protected_function_result result =
        (*def.callbacks->allowInventoryTake)(posToTable(m_lua, pos), listname, index, stack, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua allow_inventory_take error for '{}': {}", def.stringId, err.what());
        return static_cast<int>(stack.count);
    }

    return result.get_type() == sol::type::number ? result.get<int>() : static_cast<int>(stack.count);
}

int BlockCallbackInvoker::invokeAllowInventoryMove(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& fromList,
    size_t fromIdx,
    const std::string& toList,
    size_t toIdx,
    int count,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->allowInventoryMove.has_value())
    {
        return count;
    }

    sol::protected_function_result result =
        (*def.callbacks->allowInventoryMove)(posToTable(m_lua, pos), fromList, fromIdx, toList, toIdx, count, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua allow_inventory_move error for '{}': {}", def.stringId, err.what());
        return count;
    }

    return result.get_type() == sol::type::number ? result.get<int>() : count;
}

void BlockCallbackInvoker::invokeOnInventoryPut(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& listname,
    size_t index,
    const world::ItemStack& stack,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onInventoryPut.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onInventoryPut)(posToTable(m_lua, pos), listname, index, stack, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_inventory_put error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeOnInventoryTake(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& listname,
    size_t index,
    const world::ItemStack& stack,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onInventoryTake.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onInventoryTake)(posToTable(m_lua, pos), listname, index, stack, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_inventory_take error for '{}': {}", def.stringId, err.what());
    }
}

void BlockCallbackInvoker::invokeOnInventoryMove(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& fromList,
    size_t fromIdx,
    const std::string& toList,
    size_t toIdx,
    int count,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onInventoryMove.has_value())
    {
        return;
    }

    sol::protected_function_result result =
        (*def.callbacks->onInventoryMove)(posToTable(m_lua, pos), fromList, fromIdx, toList, toIdx, count, playerId);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_inventory_move error for '{}': {}", def.stringId, err.what());
    }
}

// --- Visual/client callbacks ---

void BlockCallbackInvoker::invokeOnAnimateTick(
    const world::BlockDefinition& def, const glm::ivec3& pos, sol::object randomFn)
{
    if (!def.callbacks || !def.callbacks->onAnimateTick.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->onAnimateTick)(posToTable(m_lua, pos), randomFn);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_animate_tick error for '{}': {}", def.stringId, err.what());
    }
}

std::optional<uint32_t> BlockCallbackInvoker::invokeGetColor(
    const world::BlockDefinition& def, const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->getColor.has_value())
    {
        return std::nullopt;
    }

    sol::protected_function_result result = (*def.callbacks->getColor)(posToTable(m_lua, pos));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua get_color error for '{}': {}", def.stringId, err.what());
        return std::nullopt;
    }

    if (result.get_type() == sol::type::number)
    {
        return static_cast<uint32_t>(result.get<int>());
    }
    return std::nullopt;
}

std::string BlockCallbackInvoker::invokeOnPickBlock(
    const world::BlockDefinition& def, const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->onPickBlock.has_value())
    {
        return def.stringId;
    }

    sol::protected_function_result result = (*def.callbacks->onPickBlock)(posToTable(m_lua, pos));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_pick_block error for '{}': {}", def.stringId, err.what());
        return def.stringId;
    }

    if (result.get_type() == sol::type::string)
    {
        return result.get<std::string>();
    }
    return def.stringId;
}

// --- Neighbor change callbacks ---

void BlockCallbackInvoker::invokeOnNeighborChanged(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const glm::ivec3& neighborPos,
    const std::string& neighborNode)
{
    if (!def.callbacks || !def.callbacks->onNeighborChanged.has_value())
    {
        return;
    }

    sol::protected_function_result result = (*def.callbacks->onNeighborChanged)(
        posToTable(m_lua, pos), posToTable(m_lua, neighborPos), neighborNode);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_neighbor_changed error for '{}': {}", def.stringId, err.what());
    }
}

std::optional<sol::object> BlockCallbackInvoker::invokeUpdateShape(
    const world::BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& direction,
    sol::object neighborState)
{
    if (!def.callbacks || !def.callbacks->updateShape.has_value())
    {
        return std::nullopt;
    }

    sol::protected_function_result result =
        (*def.callbacks->updateShape)(posToTable(m_lua, pos), direction, neighborState);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua update_shape error for '{}': {}", def.stringId, err.what());
        return std::nullopt;
    }

    if (result.get_type() == sol::type::lua_nil)
    {
        return std::nullopt;
    }

    return result.get<sol::object>();
}

bool BlockCallbackInvoker::invokeCanSurvive(const world::BlockDefinition& def, const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->canSurvive.has_value())
    {
        return true;
    }

    sol::protected_function_result result = (*def.callbacks->canSurvive)(posToTable(m_lua, pos));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua can_survive error for '{}': {}", def.stringId, err.what());
        return true;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : true;
}

// --- Shape callbacks ---

std::vector<math::AABB> BlockCallbackInvoker::parseBoxList(const sol::table& table)
{
    std::vector<math::AABB> boxes;
    for (auto& [key, value] : table)
    {
        if (value.get_type() != sol::type::table)
        {
            continue;
        }

        sol::table box = value.as<sol::table>();
        if (box.size() < 6)
        {
            continue;
        }

        math::AABB aabb;
        aabb.min.x = box[1].get_or(0.0f);
        aabb.min.y = box[2].get_or(0.0f);
        aabb.min.z = box[3].get_or(0.0f);
        aabb.max.x = box[4].get_or(1.0f);
        aabb.max.y = box[5].get_or(1.0f);
        aabb.max.z = box[6].get_or(1.0f);
        boxes.push_back(aabb);
    }
    return boxes;
}

std::vector<math::AABB> BlockCallbackInvoker::invokeGetCollisionShape(
    const world::BlockDefinition& def, const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->getCollisionShape.has_value())
    {
        return {};
    }

    sol::protected_function_result result = (*def.callbacks->getCollisionShape)(posToTable(m_lua, pos));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua get_collision_shape error for '{}': {}", def.stringId, err.what());
        return {};
    }

    if (result.get_type() != sol::type::table)
    {
        return {};
    }

    return parseBoxList(result.get<sol::table>());
}

std::vector<math::AABB> BlockCallbackInvoker::invokeGetSelectionShape(
    const world::BlockDefinition& def, const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->getSelectionShape.has_value())
    {
        return {};
    }

    sol::protected_function_result result = (*def.callbacks->getSelectionShape)(posToTable(m_lua, pos));
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua get_selection_shape error for '{}': {}", def.stringId, err.what());
        return {};
    }

    if (result.get_type() != sol::type::table)
    {
        return {};
    }

    return parseBoxList(result.get<sol::table>());
}

bool BlockCallbackInvoker::invokeCanAttachAt(
    const world::BlockDefinition& def, const glm::ivec3& pos, const std::string& face)
{
    if (!def.callbacks || !def.callbacks->canAttachAt.has_value())
    {
        return def.isSolid;
    }

    sol::protected_function_result result = (*def.callbacks->canAttachAt)(posToTable(m_lua, pos), face);
    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua can_attach_at error for '{}': {}", def.stringId, err.what());
        return def.isSolid;
    }

    return result.get_type() == sol::type::boolean ? result.get<bool>() : def.isSolid;
}

} // namespace voxel::scripting
