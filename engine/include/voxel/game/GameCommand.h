#pragma once

#include "voxel/core/Types.h"
#include "voxel/math/MathTypes.h"

#include <variant>

namespace voxel::game
{

/// All game action types that can flow through the command queue.
enum class CommandType : core::uint8
{
    PlaceBlock,
    BreakBlock,
    MovePlayer,
    Jump,
    ToggleSprint
};

struct PlaceBlockPayload
{
    math::IVec3 position;
    core::uint16 blockId;
};

struct BreakBlockPayload
{
    math::IVec3 position;
};

struct MovePlayerPayload
{
    math::Vec3 direction; ///< Normalized movement input vector.
    bool isSprinting;
    bool isSneaking;
};

struct JumpPayload
{
};

struct ToggleSprintPayload
{
    bool enabled;
};

/// A serializable game action. All state mutation flows through GameCommand objects
/// pushed to a CommandQueue and consumed during the simulation tick.
/// @see CommandQueue, ADR-010
struct GameCommand
{
    CommandType type;
    core::uint32 playerId;
    core::uint32 tick;
    std::variant<
        PlaceBlockPayload,
        BreakBlockPayload,
        MovePlayerPayload,
        JumpPayload,
        ToggleSprintPayload>
        payload;
};

} // namespace voxel::game
