#pragma once

#include <sol/sol.hpp>

#include <cstdint>
#include <optional>

namespace voxel::scripting
{

/// Lua callback functions attached to a block definition.
/// Stored separately from BlockDefinition to keep sol2 headers out of Block.h.
/// All callbacks are optional — absent means default engine behavior.
struct BlockCallbacks
{
    // --- Placement callbacks ---
    std::optional<sol::protected_function> canPlace;             // (pos, player) -> bool
    std::optional<sol::protected_function> getStateForPlacement; // (pos, player, pointed_thing) -> state_table|nil
    std::optional<sol::protected_function> onPlace;              // (itemstack, placer, pointed_thing) -> itemstack
    std::optional<sol::protected_function> onConstruct;          // (pos)
    std::optional<sol::protected_function> afterPlace;           // (pos, placer, itemstack, pointed_thing) -> bool
    std::optional<sol::protected_function> canBeReplaced;        // (pos, context) -> bool

    // --- Destruction callbacks ---
    std::optional<sol::protected_function> canDig;            // (pos, player) -> bool
    std::optional<sol::protected_function> onDestruct;        // (pos)
    std::optional<sol::protected_function> onDig;             // (pos, node, digger) -> bool
    std::optional<sol::protected_function> afterDestruct;     // (pos, oldnode)
    std::optional<sol::protected_function> afterDig;          // (pos, oldnode, oldmetadata, digger)
    std::optional<sol::protected_function> onBlast;           // (pos, intensity)
    std::optional<sol::protected_function> onFlood;           // (pos, oldnode, newnode) -> bool
    std::optional<sol::protected_function> preserveMetadata;  // (pos, oldnode, oldmeta, drops)
    std::optional<sol::protected_function> getDrops;          // (pos, player, tool_groups) -> table
    std::optional<sol::protected_function> getExperience;     // (pos, player, tool_groups) -> int
    std::optional<sol::protected_function> onDigProgress;     // (pos, player, progress) -> bool

    /// Quick check: returns a bitmask of which callback categories are set.
    /// Bit 0 = any placement callback, Bit 1 = any destruction callback.
    [[nodiscard]] uint8_t categoryMask() const
    {
        uint8_t mask = 0;
        if (canPlace.has_value() || getStateForPlacement.has_value() || onPlace.has_value() ||
            onConstruct.has_value() || afterPlace.has_value() || canBeReplaced.has_value())
        {
            mask |= 0x01;
        }
        if (canDig.has_value() || onDestruct.has_value() || onDig.has_value() || afterDestruct.has_value() ||
            afterDig.has_value() || onBlast.has_value() || onFlood.has_value() || preserveMetadata.has_value() ||
            getDrops.has_value() || getExperience.has_value() || onDigProgress.has_value())
        {
            mask |= 0x02;
        }
        return mask;
    }
};

} // namespace voxel::scripting
