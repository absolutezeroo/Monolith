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

} // namespace voxel::scripting
