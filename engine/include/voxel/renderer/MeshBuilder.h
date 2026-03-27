#pragma once

#include "voxel/renderer/ChunkMesh.h"

#include <array>

namespace voxel::world
{
class BlockRegistry;
struct ChunkSection;
} // namespace voxel::world

namespace voxel::renderer
{

/// Builds chunk meshes using naive face culling or binary greedy meshing.
class MeshBuilder
{
  public:
    explicit MeshBuilder(const world::BlockRegistry& registry);

    /// Build a mesh for a chunk section using naive face culling.
    /// Each visible face produces one quad (width=1, height=1, per-vertex AO computed).
    ///
    /// @param section The section to mesh (16x16x16 voxels).
    /// @param neighbors The 6 neighbor sections for boundary face culling.
    ///        Uses BlockFace ordering: [PosX, NegX, PosY, NegY, PosZ, NegZ].
    ///        nullptr = treat boundary as air (emit face).
    /// @return The generated mesh data.
    [[nodiscard]] ChunkMesh buildNaive(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors) const;

    /// Build a mesh for a chunk section using binary greedy meshing.
    /// Merges coplanar adjacent faces of the same block type into larger quads.
    /// Same inputs/output as buildNaive() — callers can swap freely.
    ///
    /// @param section The section to mesh (16x16x16 voxels).
    /// @param neighbors The 6 neighbor sections for boundary face culling.
    ///        Uses BlockFace ordering: [PosX, NegX, PosY, NegY, PosZ, NegZ].
    ///        nullptr = treat boundary as air (emit face).
    /// @return The generated mesh data with merged quads.
    [[nodiscard]] ChunkMesh buildGreedy(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors) const;

  private:
    const world::BlockRegistry& m_registry;

    /// Get the block ID adjacent to (x,y,z) in the given face direction.
    /// Returns the block from the neighbor section for boundary coordinates.
    [[nodiscard]] uint16_t getAdjacentBlock(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors,
        int x,
        int y,
        int z,
        BlockFace face) const;
};

} // namespace voxel::renderer
