#pragma once

#include "voxel/world/Block.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace voxel::game
{

/// Minimum time to break any block, even with hardness 0.
static constexpr float MIN_BREAK_TIME = 0.05f;

/// Maximum number of crack overlay stages (0-9).
static constexpr int MAX_CRACK_STAGES = 10;

/// Tracks the progress of mining a single block.
struct MiningState
{
    glm::ivec3 targetBlock{0};
    float progress = 0.0f;
    float breakTime = 0.0f;
    int crackStage = -1; ///< -1 = not mining, 0-9 = crack overlay stage.
    bool isMining = false;

    /// Reset all mining state to idle.
    void reset()
    {
        targetBlock = glm::ivec3{0};
        progress = 0.0f;
        breakTime = 0.0f;
        crackStage = -1;
        isMining = false;
    }
};

/// Calculate break time for a block definition (V1: bare hands).
/// @return breakTime = hardness * 1.5, clamped to MIN_BREAK_TIME.
[[nodiscard]] inline float calculateBreakTime(const world::BlockDefinition& def)
{
    return std::max(def.hardness * 1.5f, MIN_BREAK_TIME);
}

} // namespace voxel::game
