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

    // --- Timer callbacks ---
    std::optional<sol::protected_function> onTimer; // (pos, elapsed) -> bool (true = restart, false = stop)

    // --- Interaction callbacks ---
    std::optional<sol::protected_function> onRightclick;      // (pos, node, clicker, itemstack, pointed_thing) -> itemstack
    std::optional<sol::protected_function> onPunch;           // (pos, node, puncher, pointed_thing)
    std::optional<sol::protected_function> onSecondaryUse;    // (itemstack, user, pointed_thing) -> itemstack
    std::optional<sol::protected_function> onInteractStart;   // (pos, player) -> bool
    std::optional<sol::protected_function> onInteractStep;    // (pos, player, elapsed_seconds) -> bool
    std::optional<sol::protected_function> onInteractStop;    // (pos, player, elapsed_seconds)
    std::optional<sol::protected_function> onInteractCancel;  // (pos, player, elapsed_seconds, reason) -> bool

    // --- Neighbor change callbacks ---
    std::optional<sol::protected_function> onNeighborChanged;  // (pos, neighbor_pos, neighbor_node)
    std::optional<sol::protected_function> updateShape;        // (pos, direction, neighbor_state) -> state|nil
    std::optional<sol::protected_function> canSurvive;         // (pos) -> bool

    // --- Physics/collision shape callbacks ---
    std::optional<sol::protected_function> getCollisionShape;  // (pos) -> table of {x1,y1,z1,x2,y2,z2}
    std::optional<sol::protected_function> getSelectionShape;  // (pos) -> table of {x1,y1,z1,x2,y2,z2}
    std::optional<sol::protected_function> canAttachAt;        // (pos, face_string) -> bool
    std::optional<sol::protected_function> isPathfindable;     // (pos, pathtype) -> bool

    // --- Entity-block interaction callbacks ---
    std::optional<sol::protected_function> onEntityInside;   // (pos, entity) — fires each tick when AABB overlaps
    std::optional<sol::protected_function> onEntityStepOn;   // (pos, entity) — fires once on landing
    std::optional<sol::protected_function> onEntityFallOn;   // (pos, entity, fall_distance) -> float damage multiplier
    std::optional<sol::protected_function> onEntityCollide;  // (pos, entity, facing, velocity, is_impact)
    std::optional<sol::protected_function> onProjectileHit;  // (pos, projectile, hit_result) — V1 stub, never invoked

    // --- Inventory callbacks ---
    std::optional<sol::protected_function> allowInventoryPut;   // (pos, listname, index, stack, player) -> int
    std::optional<sol::protected_function> allowInventoryTake;  // (pos, listname, index, stack, player) -> int
    std::optional<sol::protected_function> allowInventoryMove;  // (pos, from_list, from_idx, to_list, to_idx, count, player) -> int
    std::optional<sol::protected_function> onInventoryPut;      // (pos, listname, index, stack, player)
    std::optional<sol::protected_function> onInventoryTake;     // (pos, listname, index, stack, player)
    std::optional<sol::protected_function> onInventoryMove;     // (pos, from_list, from_idx, to_list, to_idx, count, player)

    // --- Signal/power stubs ---
    std::optional<sol::protected_function> onPowered;           // (pos, power_level, source_pos)
    std::optional<sol::protected_function> getComparatorOutput; // (pos) -> int (0-15)
    std::optional<sol::protected_function> getPushReaction;     // (pos) -> string

    /// Quick check: returns a bitmask of which callback categories are set.
    /// Bit 0 = placement, Bit 1 = destruction, Bit 2 = interaction, Bit 3 = timer,
    /// Bit 4 = neighbor, Bit 5 = shape, Bit 6 = signal, Bit 7 = entity, Bit 8 = inventory.
    [[nodiscard]] uint16_t categoryMask() const
    {
        uint16_t mask = 0;
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
        if (onRightclick.has_value() || onPunch.has_value() || onSecondaryUse.has_value() ||
            onInteractStart.has_value() || onInteractStep.has_value() || onInteractStop.has_value() ||
            onInteractCancel.has_value())
        {
            mask |= 0x04;
        }
        if (onTimer.has_value())
        {
            mask |= 0x08;
        }
        if (onNeighborChanged.has_value() || updateShape.has_value() || canSurvive.has_value())
        {
            mask |= 0x10;
        }
        if (getCollisionShape.has_value() || getSelectionShape.has_value() || canAttachAt.has_value() ||
            isPathfindable.has_value())
        {
            mask |= 0x20;
        }
        if (onPowered.has_value() || getComparatorOutput.has_value() || getPushReaction.has_value())
        {
            mask |= 0x40;
        }
        if (onEntityInside.has_value() || onEntityStepOn.has_value() || onEntityFallOn.has_value() ||
            onEntityCollide.has_value() || onProjectileHit.has_value())
        {
            mask |= 0x80;
        }
        if (allowInventoryPut.has_value() || allowInventoryTake.has_value() || allowInventoryMove.has_value() ||
            onInventoryPut.has_value() || onInventoryTake.has_value() || onInventoryMove.has_value())
        {
            mask |= 0x100;
        }
        return mask;
    }
};

} // namespace voxel::scripting
