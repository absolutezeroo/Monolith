#include "voxel/scripting/GlobalEventRegistry.h"

#include "voxel/core/Log.h"

namespace voxel::scripting
{

// All 41 named events across 7 categories.
const std::unordered_set<std::string> GlobalEventRegistry::VALID_EVENTS = {
    // Player Events (12)
    "player_join",
    "player_leave",
    "player_respawn",
    "player_damage",
    "player_death",
    "player_move",
    "player_jump",
    "player_land",
    "player_sprint_toggle",
    "player_sneak_toggle",
    "player_interact",
    "player_hotbar_changed",

    // Block Events (6)
    "block_placed",
    "block_broken",
    "block_changed",
    "block_neighbor_changed",
    "block_dig_start",
    "block_timer_fired",

    // World Events (5)
    "chunk_loaded",
    "chunk_unloaded",
    "chunk_generated",
    "world_saved",
    "section_meshed",

    // Time Events (3)
    "tick",
    "day_phase_changed",
    "new_day",

    // Input Events (10)
    "key_pressed",
    "key_released",
    "key_held",
    "key_double_tap",
    "mouse_click",
    "mouse_released",
    "mouse_held",
    "scroll_wheel",
    "combo_triggered",
    "hotbar_changed",

    // Engine Events (3)
    "shutdown",
    "hot_reload_start",
    "hot_reload_complete",

    // Mod Events (2)
    "mod_loaded",
    "all_mods_loaded",
};

void GlobalEventRegistry::registerCallback(const std::string& eventName, sol::protected_function callback)
{
    if (VALID_EVENTS.find(eventName) == VALID_EVENTS.end())
    {
        VX_LOG_WARN("voxel.on(): unknown event name '{}' — registering anyway", eventName);
    }
    m_callbacks[eventName].push_back(std::move(callback));
}

void GlobalEventRegistry::clearAll()
{
    m_callbacks.clear();
}

size_t GlobalEventRegistry::callbackCount(const std::string& eventName) const
{
    auto it = m_callbacks.find(eventName);
    if (it == m_callbacks.end())
    {
        return 0;
    }
    return it->second.size();
}

} // namespace voxel::scripting
