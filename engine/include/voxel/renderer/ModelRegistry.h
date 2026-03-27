#pragma once

#include "voxel/renderer/ChunkMesh.h"
#include "voxel/world/Block.h"

#include <vector>

namespace voxel::renderer
{

/// Registry for non-cubic block model geometry.
/// V1: Slab, Cross, Torch are fully functional; other types return empty (stub).
class ModelRegistry
{
  public:
    /// Generate model vertices for a given block model type.
    /// @param x Block X position within section (0-15).
    /// @param y Block Y position within section (0-15).
    /// @param z Block Z position within section (0-15).
    /// @param blockDef Block definition with model type and properties.
    /// @param state Current state values for state-dependent models.
    /// @param stateId Actual block state ID (may differ from baseStateId for state variants).
    /// @param faceMask Bitmask of visible faces (bit i = face i). 0x3F = all visible.
    /// @param outVertices Output vector to append vertices to.
    void getModelVertices(
        int x,
        int y,
        int z,
        const world::BlockDefinition& blockDef,
        const world::StateMap& state,
        uint16_t stateId,
        uint8_t faceMask,
        std::vector<ModelVertex>& outVertices) const;
};

} // namespace voxel::renderer
