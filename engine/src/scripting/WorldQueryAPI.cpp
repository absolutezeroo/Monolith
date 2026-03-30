#include "voxel/scripting/WorldQueryAPI.h"

#include "voxel/core/Log.h"
#include "voxel/physics/Raycast.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/BlockTimerManager.h"
#include "voxel/scripting/ShapeCache.h"
#include "voxel/world/BiomeSystem.h"
#include "voxel/world/BiomeTypes.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace voxel::scripting
{

// Static member definitions
float WorldQueryAPI::s_timeOfDay = 0.5f; // noon default
std::unordered_map<std::string, std::string> WorldQueryAPI::s_settings;
std::unordered_set<std::string> WorldQueryAPI::s_writableSettings = {
    "time_speed",
    "render_distance",
};

// ============================================================================
// Helpers
// ============================================================================

glm::ivec3 WorldQueryAPI::tableToPos(const sol::table& table)
{
    return {table.get<int>("x"), table.get<int>("y"), table.get<int>("z")};
}

sol::table WorldQueryAPI::posToTable(sol::state& lua, const glm::ivec3& pos)
{
    return lua.create_table_with("x", pos.x, "y", pos.y, "z", pos.z);
}

bool WorldQueryAPI::isPositionLoaded(const glm::ivec3& pos, const world::ChunkManager& chunkMgr)
{
    if (pos.y < 0 || pos.y >= world::ChunkColumn::COLUMN_HEIGHT)
    {
        return false;
    }
    glm::ivec2 coord = world::worldToChunkCoord(pos);
    return chunkMgr.getChunk(coord) != nullptr;
}

bool WorldQueryAPI::matchesFilter(
    uint16_t blockId,
    const std::string& filter,
    const world::BlockRegistry& registry)
{
    // Group matching: "group:name"
    if (filter.size() > 6 && filter.substr(0, 6) == "group:")
    {
        std::string groupName = filter.substr(6);
        const auto& def = registry.getBlockType(blockId);
        return def.groups.count(groupName) > 0;
    }

    // Exact string ID match
    uint16_t filterId = registry.getIdByName(filter);
    if (filterId == world::BLOCK_AIR && filter != "air")
    {
        return false; // unknown filter
    }

    // Compare base block type (ignore state variants)
    const auto& blockDef = registry.getBlockType(blockId);
    const auto& filterDef = registry.getBlockType(filterId);
    return blockDef.stringId == filterDef.stringId;
}

// ============================================================================
// Registration entry point
// ============================================================================

void WorldQueryAPI::registerWorldAPI(
    sol::state& lua,
    world::ChunkManager& chunkMgr,
    world::BlockRegistry& registry,
    BlockCallbackInvoker& invoker,
    BlockTimerManager& timerMgr,
    RateLimiter& rateLimiter,
    const world::BiomeSystem* biomeSystem,
    ShapeCache* shapeCache)
{
    sol::table voxelTable = lua["voxel"];

    registerBlockQueries(lua, voxelTable, chunkMgr, registry);
    registerBlockModifications(lua, voxelTable, chunkMgr, registry, invoker, rateLimiter);
    registerAreaSearch(lua, voxelTable, chunkMgr, registry, rateLimiter);
    registerRaycast(voxelTable, lua, chunkMgr, registry, shapeCache, rateLimiter);
    registerEnvironmentQueries(lua, voxelTable, chunkMgr, biomeSystem);
    registerScheduledTicks(lua, voxelTable, timerMgr, rateLimiter);
    registerPatternMatching(lua, voxelTable, chunkMgr, registry, rateLimiter);
    registerSettingsAPI(lua, voxelTable);

    VX_LOG_DEBUG("WorldQueryAPI: all functions registered");
}

// ============================================================================
// Block Queries (AC: 1, 10)
// ============================================================================

void WorldQueryAPI::registerBlockQueries(
    sol::state& lua,
    sol::table& voxelTable,
    world::ChunkManager& chunkMgr,
    world::BlockRegistry& registry)
{
    // voxel.get_block(pos) -> string ID or nil
    voxelTable.set_function("get_block", [&lua, &chunkMgr, &registry](sol::table posTable) -> sol::object {
        glm::ivec3 pos = tableToPos(posTable);
        if (!isPositionLoaded(pos, chunkMgr))
        {
            return sol::lua_nil;
        }
        uint16_t blockId = chunkMgr.getBlock(pos);
        const auto& def = registry.getBlockType(blockId);
        return sol::make_object(lua, def.stringId);
    });

    // voxel.get_block_info(string_id) -> table with block definition fields, or nil
    voxelTable.set_function("get_block_info", [&lua, &registry](const std::string& stringId) -> sol::object {
        uint16_t id = registry.getIdByName(stringId);
        if (id == world::BLOCK_AIR && stringId != "air")
        {
            return sol::lua_nil;
        }
        const auto& def = registry.getBlockType(id);

        sol::table info = lua.create_table();
        info["id"] = def.stringId;
        info["numeric_id"] = def.numericId;
        info["solid"] = def.isSolid;
        info["transparent"] = def.isTransparent;
        info["has_collision"] = def.hasCollision;
        info["hardness"] = def.hardness;
        info["light_emission"] = def.lightEmission;
        info["light_filter"] = def.lightFilter;
        info["climbable"] = def.isClimbable;
        info["buildable_to"] = def.isBuildableTo;
        info["replaceable"] = def.isReplaceable;
        info["floodable"] = def.isFloodable;
        info["drop"] = def.dropItem;

        // Groups sub-table
        sol::table groups = lua.create_table();
        for (const auto& [name, level] : def.groups)
        {
            groups[name] = level;
        }
        info["groups"] = groups;

        return info;
    });

    // voxel.get_block_state(pos) -> table with state property values, or nil
    voxelTable.set_function("get_block_state", [&lua, &chunkMgr, &registry](sol::table posTable) -> sol::object {
        glm::ivec3 pos = tableToPos(posTable);
        if (!isPositionLoaded(pos, chunkMgr))
        {
            return sol::lua_nil;
        }
        uint16_t stateId = chunkMgr.getBlock(pos);
        world::StateMap stateValues = registry.getStateValues(stateId);

        if (stateValues.empty())
        {
            return sol::lua_nil;
        }

        sol::table stateTable = lua.create_table();
        for (const auto& [propName, propValue] : stateValues)
        {
            stateTable[propName] = propValue;
        }
        return stateTable;
    });
}

// ============================================================================
// Block Modifications (AC: 2, 10)
// ============================================================================

void WorldQueryAPI::registerBlockModifications(
    sol::state& lua,
    sol::table& voxelTable,
    world::ChunkManager& chunkMgr,
    world::BlockRegistry& registry,
    BlockCallbackInvoker& invoker,
    RateLimiter& rateLimiter)
{
    // voxel.set_block(pos, block_id) -> true on success, nil on failure
    voxelTable.set_function(
        "set_block",
        [&lua, &chunkMgr, &registry, &invoker, &rateLimiter](sol::table posTable, const std::string& blockId)
            -> sol::object {
            if (!rateLimiter.checkSetBlock())
            {
                return sol::lua_nil;
            }

            glm::ivec3 pos = tableToPos(posTable);
            if (!isPositionLoaded(pos, chunkMgr))
            {
                return sol::lua_nil;
            }

            uint16_t newId = registry.getIdByName(blockId);
            if (newId == world::BLOCK_AIR && blockId != "air")
            {
                VX_LOG_WARN("voxel.set_block: unknown block '{}'", blockId);
                return sol::lua_nil;
            }

            // Get old block and fire destruction callbacks
            uint16_t oldId = chunkMgr.getBlock(pos);
            const auto& oldDef = registry.getBlockType(oldId);
            if (oldDef.callbacks != nullptr)
            {
                invoker.invokeOnDestruct(oldDef, pos);
            }

            // Set the new block
            chunkMgr.setBlock(pos, newId);

            // Update lighting
            const auto& newDef = registry.getBlockType(newId);
            chunkMgr.updateLightAfterBlockChange(pos, &oldDef, &newDef);

            // Fire construction callbacks on new block
            if (newDef.callbacks != nullptr)
            {
                invoker.invokeOnConstruct(newDef, pos);
            }

            return sol::make_object(lua, true);
        });

    // voxel.set_block_state(pos, block_id, state_table) -> true on success, nil on failure
    voxelTable.set_function(
        "set_block_state",
        [&lua, &chunkMgr, &registry, &invoker, &rateLimiter](
            sol::table posTable, const std::string& blockId, sol::table stateTable) -> sol::object {
            if (!rateLimiter.checkSetBlock())
            {
                return sol::lua_nil;
            }

            glm::ivec3 pos = tableToPos(posTable);
            if (!isPositionLoaded(pos, chunkMgr))
            {
                return sol::lua_nil;
            }

            uint16_t baseId = registry.getIdByName(blockId);
            if (baseId == world::BLOCK_AIR && blockId != "air")
            {
                VX_LOG_WARN("voxel.set_block_state: unknown block '{}'", blockId);
                return sol::lua_nil;
            }

            // Build state map from Lua table
            world::StateMap stateMap;
            for (const auto& [key, value] : stateTable)
            {
                stateMap[key.as<std::string>()] = value.as<std::string>();
            }

            uint16_t stateId = registry.getStateId(baseId, stateMap);

            // Get old block and fire destruction callbacks
            uint16_t oldId = chunkMgr.getBlock(pos);
            const auto& oldDef = registry.getBlockType(oldId);
            if (oldDef.callbacks != nullptr)
            {
                invoker.invokeOnDestruct(oldDef, pos);
            }

            chunkMgr.setBlock(pos, stateId);

            const auto& newDef = registry.getBlockType(stateId);
            chunkMgr.updateLightAfterBlockChange(pos, &oldDef, &newDef);

            if (newDef.callbacks != nullptr)
            {
                invoker.invokeOnConstruct(newDef, pos);
            }

            return sol::make_object(lua, true);
        });

    // voxel.dig_block(pos) -> true on success, false on failure
    voxelTable.set_function(
        "dig_block",
        [&lua, &chunkMgr, &registry, &invoker, &rateLimiter](sol::table posTable) -> sol::object {
            if (!rateLimiter.checkSetBlock())
            {
                return sol::lua_nil;
            }

            glm::ivec3 pos = tableToPos(posTable);
            if (!isPositionLoaded(pos, chunkMgr))
            {
                return sol::make_object(lua, false);
            }

            uint16_t oldId = chunkMgr.getBlock(pos);
            if (oldId == world::BLOCK_AIR)
            {
                return sol::make_object(lua, false);
            }

            const auto& def = registry.getBlockType(oldId);

            // Check canDig (playerId 0 = scripted)
            if (def.callbacks != nullptr)
            {
                bool canDig = invoker.invokeCanDig(def, pos, 0);
                if (!canDig)
                {
                    return sol::make_object(lua, false);
                }
            }

            // Fire onDestruct
            if (def.callbacks != nullptr)
            {
                invoker.invokeOnDestruct(def, pos);
            }

            // Set to air
            chunkMgr.setBlock(pos, world::BLOCK_AIR);

            // Update lighting
            chunkMgr.updateLightAfterBlockChange(pos, &def, nullptr);

            // Fire afterDestruct
            if (def.callbacks != nullptr)
            {
                invoker.invokeAfterDestruct(def, pos, oldId);
            }

            return sol::make_object(lua, true);
        });

    // voxel.swap_block(pos, new_id) -> true on success, nil on failure
    // Raw setBlock WITHOUT callbacks — for state changes like door open/close
    voxelTable.set_function(
        "swap_block",
        [&lua, &chunkMgr, &registry, &rateLimiter](sol::table posTable, const std::string& blockId) -> sol::object {
            if (!rateLimiter.checkSetBlock())
            {
                return sol::lua_nil;
            }

            glm::ivec3 pos = tableToPos(posTable);
            if (!isPositionLoaded(pos, chunkMgr))
            {
                return sol::lua_nil;
            }

            uint16_t newId = registry.getIdByName(blockId);
            if (newId == world::BLOCK_AIR && blockId != "air")
            {
                VX_LOG_WARN("voxel.swap_block: unknown block '{}'", blockId);
                return sol::lua_nil;
            }

            uint16_t oldId = chunkMgr.getBlock(pos);
            const auto& oldDef = registry.getBlockType(oldId);
            const auto& newDef = registry.getBlockType(newId);

            chunkMgr.setBlock(pos, newId);
            chunkMgr.updateLightAfterBlockChange(pos, &oldDef, &newDef);

            return sol::make_object(lua, true);
        });
}

// ============================================================================
// Area Search (AC: 3)
// ============================================================================

void WorldQueryAPI::registerAreaSearch(
    sol::state& lua,
    sol::table& voxelTable,
    world::ChunkManager& chunkMgr,
    world::BlockRegistry& registry,
    RateLimiter& rateLimiter)
{
    // voxel.find_blocks_in_area(p1, p2, filter) -> table of positions, or nil
    voxelTable.set_function(
        "find_blocks_in_area",
        [&lua, &chunkMgr, &registry, &rateLimiter](
            sol::table p1Table, sol::table p2Table, const std::string& filter) -> sol::object {
            if (!rateLimiter.checkFindArea())
            {
                return sol::lua_nil;
            }

            glm::ivec3 p1 = tableToPos(p1Table);
            glm::ivec3 p2 = tableToPos(p2Table);

            // Normalize min/max
            glm::ivec3 minPos = glm::min(p1, p2);
            glm::ivec3 maxPos = glm::max(p1, p2);

            sol::table results = lua.create_table();
            int count = 0;

            for (int y = minPos.y; y <= maxPos.y; ++y)
            {
                for (int z = minPos.z; z <= maxPos.z; ++z)
                {
                    for (int x = minPos.x; x <= maxPos.x; ++x)
                    {
                        glm::ivec3 pos{x, y, z};
                        if (!isPositionLoaded(pos, chunkMgr))
                        {
                            continue;
                        }
                        uint16_t blockId = chunkMgr.getBlock(pos);
                        if (matchesFilter(blockId, filter, registry))
                        {
                            results[++count] = posToTable(lua, pos);
                        }
                    }
                }
            }

            return results;
        });

    // voxel.count_blocks_in_area(p1, p2, filter) -> count (int)
    voxelTable.set_function(
        "count_blocks_in_area",
        [&lua, &chunkMgr, &registry, &rateLimiter](
            sol::table p1Table, sol::table p2Table, const std::string& filter) -> sol::object {
            if (!rateLimiter.checkFindArea())
            {
                return sol::lua_nil;
            }

            glm::ivec3 p1 = tableToPos(p1Table);
            glm::ivec3 p2 = tableToPos(p2Table);

            glm::ivec3 minPos = glm::min(p1, p2);
            glm::ivec3 maxPos = glm::max(p1, p2);

            int count = 0;
            for (int y = minPos.y; y <= maxPos.y; ++y)
            {
                for (int z = minPos.z; z <= maxPos.z; ++z)
                {
                    for (int x = minPos.x; x <= maxPos.x; ++x)
                    {
                        glm::ivec3 pos{x, y, z};
                        if (!isPositionLoaded(pos, chunkMgr))
                        {
                            continue;
                        }
                        uint16_t blockId = chunkMgr.getBlock(pos);
                        if (matchesFilter(blockId, filter, registry))
                        {
                            ++count;
                        }
                    }
                }
            }

            return sol::make_object(lua, count);
        });
}

// ============================================================================
// Raycasting (AC: 4)
// ============================================================================

void WorldQueryAPI::registerRaycast(
    sol::table& voxelTable,
    sol::state& lua,
    world::ChunkManager& chunkMgr,
    world::BlockRegistry& registry,
    ShapeCache* shapeCache,
    RateLimiter& rateLimiter)
{
    voxelTable.set_function(
        "raycast",
        [&lua, &chunkMgr, &registry, shapeCache, &rateLimiter](
            float ox, float oy, float oz, float dx, float dy, float dz, float maxDist) -> sol::object {
            if (!rateLimiter.checkRaycast())
            {
                return sol::lua_nil;
            }

            glm::vec3 origin{ox, oy, oz};
            glm::vec3 direction{dx, dy, dz};

            // Normalize direction
            float len = glm::length(direction);
            if (len < 1e-6f)
            {
                return sol::lua_nil;
            }
            direction /= len;

            physics::RaycastResult result = physics::raycast(origin, direction, maxDist, chunkMgr, registry, shapeCache);

            if (!result.hit)
            {
                return sol::lua_nil;
            }

            sol::table hitInfo = lua.create_table();
            hitInfo["pos"] = posToTable(lua, result.blockPos);
            hitInfo["face"] = static_cast<int>(result.face);
            hitInfo["distance"] = result.distance;

            const auto& def = registry.getBlockType(chunkMgr.getBlock(result.blockPos));
            hitInfo["block_id"] = def.stringId;

            return hitInfo;
        });
}

// ============================================================================
// Environment Queries (AC: 5)
// ============================================================================

void WorldQueryAPI::registerEnvironmentQueries(
    sol::state& lua,
    sol::table& voxelTable,
    world::ChunkManager& chunkMgr,
    const world::BiomeSystem* biomeSystem)
{
    // voxel.get_biome(x, z) -> string biome name
    voxelTable.set_function("get_biome", [&lua, biomeSystem](int x, int z) -> sol::object {
        if (biomeSystem == nullptr)
        {
            return sol::make_object(lua, std::string("unknown"));
        }

        world::BiomeType biome = biomeSystem->getBiomeAt(static_cast<float>(x), static_cast<float>(z));
        return sol::make_object(lua, world::biomeToString(biome));
    });

    // voxel.get_light(pos) -> {sky=N, block=N} or nil
    voxelTable.set_function("get_light", [&lua, &chunkMgr](sol::table posTable) -> sol::object {
        glm::ivec3 pos = tableToPos(posTable);
        if (!isPositionLoaded(pos, chunkMgr))
        {
            return sol::lua_nil;
        }

        glm::ivec2 coord = world::worldToChunkCoord(pos);
        const auto* column = chunkMgr.getChunk(coord);
        if (column == nullptr)
        {
            return sol::lua_nil;
        }

        int sectionY = pos.y / world::ChunkSection::SIZE;
        int localX = world::euclideanMod(pos.x, world::ChunkSection::SIZE);
        int localY = pos.y % world::ChunkSection::SIZE;
        int localZ = world::euclideanMod(pos.z, world::ChunkSection::SIZE);

        const auto& lightMap = column->getLightMap(sectionY);

        sol::table lightInfo = lua.create_table();
        lightInfo["sky"] = lightMap.getSkyLight(localX, localY, localZ);
        lightInfo["block"] = lightMap.getBlockLight(localX, localY, localZ);
        return lightInfo;
    });

    // voxel.get_time_of_day() -> float [0.0, 1.0)
    voxelTable.set_function("get_time_of_day", []() -> float { return s_timeOfDay; });

    // voxel.set_time_of_day(float) -> nil
    voxelTable.set_function("set_time_of_day", [](float time) { s_timeOfDay = std::fmod(std::abs(time), 1.0f); });
}

// ============================================================================
// Scheduled Ticks (AC: 6)
// ============================================================================

void WorldQueryAPI::registerScheduledTicks(
    sol::state& lua,
    sol::table& voxelTable,
    BlockTimerManager& timerMgr,
    RateLimiter& rateLimiter)
{
    // voxel.schedule_tick(pos, delay_ticks, priority) -> true or nil
    voxelTable.set_function(
        "schedule_tick",
        [&lua, &timerMgr, &rateLimiter](sol::table posTable, int delayTicks, sol::optional<int> priority)
            -> sol::object {
            if (!rateLimiter.checkSchedule())
            {
                return sol::lua_nil;
            }

            glm::ivec3 pos = tableToPos(posTable);
            int prio = priority.value_or(0);

            timerMgr.scheduleTick(pos, delayTicks, prio);
            return sol::make_object(lua, true);
        });

    // voxel.set_node_timer_active(pos, bool) -> nil
    voxelTable.set_function(
        "set_node_timer_active",
        [&timerMgr](sol::table posTable, bool active) {
            glm::ivec3 pos = tableToPos(posTable);
            timerMgr.setTimerActive(pos, active);
        });
}

// ============================================================================
// Pattern Matching (AC: 7)
// ============================================================================

void WorldQueryAPI::registerPatternMatching(
    sol::state& lua,
    sol::table& voxelTable,
    world::ChunkManager& chunkMgr,
    world::BlockRegistry& registry,
    RateLimiter& rateLimiter)
{
    // voxel.check_pattern(pos, entries) -> true if all match, false otherwise
    voxelTable.set_function(
        "check_pattern",
        [&lua, &chunkMgr, &registry, &rateLimiter](sol::table posTable, sol::table entries) -> sol::object {
            if (!rateLimiter.checkPattern())
            {
                return sol::lua_nil;
            }

            glm::ivec3 basePos = tableToPos(posTable);

            for (const auto& [key, value] : entries)
            {
                sol::table entry = value.as<sol::table>();
                int dx = entry.get<int>("x");
                int dy = entry.get<int>("y");
                int dz = entry.get<int>("z");
                std::string filter = entry.get<std::string>("name");

                glm::ivec3 checkPos = basePos + glm::ivec3{dx, dy, dz};

                if (!isPositionLoaded(checkPos, chunkMgr))
                {
                    return sol::make_object(lua, false);
                }

                uint16_t blockId = chunkMgr.getBlock(checkPos);
                if (!matchesFilter(blockId, filter, registry))
                {
                    return sol::make_object(lua, false);
                }
            }

            return sol::make_object(lua, true);
        });

    // voxel.check_box_pattern(p1, p2, filter, opts) -> {match=bool, count=int}
    voxelTable.set_function(
        "check_box_pattern",
        [&lua, &chunkMgr, &registry, &rateLimiter](
            sol::table p1Table, sol::table p2Table, const std::string& filter, sol::optional<sol::table> opts)
            -> sol::object {
            if (!rateLimiter.checkPattern())
            {
                return sol::lua_nil;
            }

            glm::ivec3 p1 = tableToPos(p1Table);
            glm::ivec3 p2 = tableToPos(p2Table);

            glm::ivec3 minPos = glm::min(p1, p2);
            glm::ivec3 maxPos = glm::max(p1, p2);

            bool allowMixed = false;
            if (opts.has_value())
            {
                allowMixed = opts->get_or("allow_mixed", false);
            }

            int matchCount = 0;
            int totalCount = 0;

            for (int y = minPos.y; y <= maxPos.y; ++y)
            {
                for (int z = minPos.z; z <= maxPos.z; ++z)
                {
                    for (int x = minPos.x; x <= maxPos.x; ++x)
                    {
                        glm::ivec3 pos{x, y, z};
                        ++totalCount;

                        if (!isPositionLoaded(pos, chunkMgr))
                        {
                            if (!allowMixed)
                            {
                                sol::table result = lua.create_table();
                                result["match"] = false;
                                result["count"] = matchCount;
                                return result;
                            }
                            continue;
                        }

                        uint16_t blockId = chunkMgr.getBlock(pos);
                        if (matchesFilter(blockId, filter, registry))
                        {
                            ++matchCount;
                        }
                        else if (!allowMixed)
                        {
                            sol::table result = lua.create_table();
                            result["match"] = false;
                            result["count"] = matchCount;
                            return result;
                        }
                    }
                }
            }

            sol::table result = lua.create_table();
            result["match"] = (matchCount == totalCount);
            result["count"] = matchCount;
            return result;
        });

    // voxel.check_ring(pos, y_offset, radius, filter) -> {complete=bool, count=int, total=int}
    voxelTable.set_function(
        "check_ring",
        [&lua, &chunkMgr, &registry, &rateLimiter](
            sol::table posTable, int yOffset, int radius, const std::string& filter) -> sol::object {
            if (!rateLimiter.checkPattern())
            {
                return sol::lua_nil;
            }

            glm::ivec3 basePos = tableToPos(posTable);
            int y = basePos.y + yOffset;
            int cx = basePos.x;
            int cz = basePos.z;

            int matchCount = 0;
            int totalCount = 0;

            // Iterate the 4 edges of the square ring
            for (int i = -radius; i <= radius; ++i)
            {
                // Top edge: (cx+i, y, cz-radius)
                // Bottom edge: (cx+i, y, cz+radius)
                // Left edge: (cx-radius, y, cz+i) — skip corners (already counted)
                // Right edge: (cx+radius, y, cz+i) — skip corners (already counted)

                glm::ivec3 positions[4] = {
                    {cx + i, y, cz - radius},
                    {cx + i, y, cz + radius},
                    {cx - radius, y, cz + i},
                    {cx + radius, y, cz + i},
                };

                for (int p = 0; p < 4; ++p)
                {
                    // Skip duplicate corners: left/right edges skip i == -radius and i == radius
                    if (p >= 2 && (i == -radius || i == radius))
                    {
                        continue;
                    }

                    const auto& pos = positions[p];
                    ++totalCount;

                    if (!isPositionLoaded(pos, chunkMgr))
                    {
                        continue;
                    }

                    uint16_t blockId = chunkMgr.getBlock(pos);
                    if (matchesFilter(blockId, filter, registry))
                    {
                        ++matchCount;
                    }
                }
            }

            sol::table result = lua.create_table();
            result["complete"] = (matchCount == totalCount);
            result["count"] = matchCount;
            result["total"] = totalCount;
            return result;
        });
}

// ============================================================================
// Settings API (AC: 8)
// ============================================================================

void WorldQueryAPI::registerSettingsAPI(sol::state& lua, sol::table& voxelTable)
{
    // voxel.get_setting(name) -> string value or nil
    voxelTable.set_function("get_setting", [&lua](const std::string& name) -> sol::object {
        auto it = s_settings.find(name);
        if (it == s_settings.end())
        {
            return sol::lua_nil;
        }
        return sol::make_object(lua, it->second);
    });

    // voxel.set_setting(name, value) -> true or nil
    voxelTable.set_function("set_setting", [](const std::string& name, const std::string& value) -> bool {
        if (s_writableSettings.count(name) == 0)
        {
            VX_LOG_WARN("voxel.set_setting: '{}' is not a writable setting", name);
            return false;
        }
        s_settings[name] = value;
        return true;
    });
}

} // namespace voxel::scripting
