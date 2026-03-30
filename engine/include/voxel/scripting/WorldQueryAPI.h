#pragma once

#include "voxel/core/Log.h"

#include <sol/forward.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace voxel::world
{
class BlockRegistry;
class ChunkManager;
class BiomeSystem;
} // namespace voxel::world

namespace voxel::scripting
{
class BlockCallbackInvoker;
class BlockTimerManager;
class ShapeCache;

/// Per-tick rate limiter for expensive Lua world-query functions.
/// V1 uses a single global counter set. When multi-mod support arrives (9.11),
/// promote to per-mod tracking.
struct RateLimiter
{
    static constexpr int SET_BLOCK_LIMIT = 1000;
    static constexpr int RAYCAST_LIMIT = 100;
    static constexpr int FIND_AREA_LIMIT = 10;
    static constexpr int PATTERN_LIMIT = 50;
    static constexpr int SCHEDULE_LIMIT = 500;

    int setBlockCount = 0;
    int raycastCount = 0;
    int findAreaCount = 0;
    int patternCount = 0;
    int scheduleCount = 0;

    void resetTick()
    {
        setBlockCount = 0;
        raycastCount = 0;
        findAreaCount = 0;
        patternCount = 0;
        scheduleCount = 0;
    }

    [[nodiscard]] bool checkSetBlock()
    {
        if (++setBlockCount > SET_BLOCK_LIMIT)
        {
            VX_LOG_WARN("Rate limit exceeded: set_block ({}/tick)", SET_BLOCK_LIMIT);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool checkRaycast()
    {
        if (++raycastCount > RAYCAST_LIMIT)
        {
            VX_LOG_WARN("Rate limit exceeded: raycast ({}/tick)", RAYCAST_LIMIT);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool checkFindArea()
    {
        if (++findAreaCount > FIND_AREA_LIMIT)
        {
            VX_LOG_WARN("Rate limit exceeded: find_blocks_in_area ({}/tick)", FIND_AREA_LIMIT);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool checkPattern()
    {
        if (++patternCount > PATTERN_LIMIT)
        {
            VX_LOG_WARN("Rate limit exceeded: pattern check ({}/tick)", PATTERN_LIMIT);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool checkSchedule()
    {
        if (++scheduleCount > SCHEDULE_LIMIT)
        {
            VX_LOG_WARN("Rate limit exceeded: schedule_tick ({}/tick)", SCHEDULE_LIMIT);
            return false;
        }
        return true;
    }
};

/// Binds world query and modification functions to the Lua `voxel` table.
/// Follows the same static registration pattern as LuaBindings.
class WorldQueryAPI
{
public:
    /// Register all world query/modification functions onto the existing `voxel` table.
    /// @param lua          The Lua state (must already have a `voxel` table from ScriptEngine::init).
    /// @param chunkMgr     ChunkManager for block access.
    /// @param registry     BlockRegistry for ID resolution.
    /// @param invoker      BlockCallbackInvoker for callback chains.
    /// @param timerMgr     BlockTimerManager for scheduled ticks.
    /// @param rateLimiter  RateLimiter reference (owned externally, reset per tick).
    /// @param biomeSystem  BiomeSystem pointer (nullable — returns "unknown" if null).
    /// @param shapeCache   ShapeCache pointer for raycasting (nullable).
    static void registerWorldAPI(
        sol::state& lua,
        world::ChunkManager& chunkMgr,
        world::BlockRegistry& registry,
        BlockCallbackInvoker& invoker,
        BlockTimerManager& timerMgr,
        RateLimiter& rateLimiter,
        const world::BiomeSystem* biomeSystem,
        ShapeCache* shapeCache);

private:
    /// Register block query functions: get_block, get_block_info, get_block_state.
    static void registerBlockQueries(
        sol::state& lua,
        sol::table& voxelTable,
        world::ChunkManager& chunkMgr,
        world::BlockRegistry& registry);

    /// Register block modification functions: set_block, set_block_state, dig_block, swap_block.
    static void registerBlockModifications(
        sol::state& lua,
        sol::table& voxelTable,
        world::ChunkManager& chunkMgr,
        world::BlockRegistry& registry,
        BlockCallbackInvoker& invoker,
        RateLimiter& rateLimiter);

    /// Register area search functions: find_blocks_in_area, count_blocks_in_area.
    static void registerAreaSearch(
        sol::state& lua,
        sol::table& voxelTable,
        world::ChunkManager& chunkMgr,
        world::BlockRegistry& registry,
        RateLimiter& rateLimiter);

    /// Register raycasting function.
    static void registerRaycast(
        sol::table& voxelTable,
        sol::state& lua,
        world::ChunkManager& chunkMgr,
        world::BlockRegistry& registry,
        ShapeCache* shapeCache,
        RateLimiter& rateLimiter);

    /// Register biome/lighting/time queries.
    static void registerEnvironmentQueries(
        sol::state& lua,
        sol::table& voxelTable,
        world::ChunkManager& chunkMgr,
        const world::BiomeSystem* biomeSystem);

    /// Register scheduled tick extensions.
    static void registerScheduledTicks(
        sol::state& lua,
        sol::table& voxelTable,
        BlockTimerManager& timerMgr,
        RateLimiter& rateLimiter);

    /// Register multiblock pattern matching functions.
    static void registerPatternMatching(
        sol::state& lua,
        sol::table& voxelTable,
        world::ChunkManager& chunkMgr,
        world::BlockRegistry& registry,
        RateLimiter& rateLimiter);

    /// Register settings API.
    static void registerSettingsAPI(sol::state& lua, sol::table& voxelTable);

    /// Helper: extract glm::ivec3 from a Lua table with x/y/z fields.
    static glm::ivec3 tableToPos(const sol::table& table);

    /// Helper: convert glm::ivec3 to a Lua table with x/y/z fields.
    static sol::table posToTable(sol::state& lua, const glm::ivec3& pos);

    /// Helper: check if a block ID matches a filter string (exact name or "group:name").
    static bool matchesFilter(
        uint16_t blockId,
        const std::string& filter,
        const world::BlockRegistry& registry);

    /// Helper: check if a position is in a loaded chunk.
    static bool isPositionLoaded(const glm::ivec3& pos, const world::ChunkManager& chunkMgr);

    /// Game time state (shared across all API calls).
    static float s_timeOfDay;

    /// Settings whitelist and storage.
    static std::unordered_map<std::string, std::string> s_settings;
    static std::unordered_set<std::string> s_writableSettings;
};

} // namespace voxel::scripting
