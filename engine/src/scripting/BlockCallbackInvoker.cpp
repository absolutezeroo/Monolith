#include "voxel/scripting/BlockCallbackInvoker.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbacks.h"
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

} // namespace voxel::scripting
