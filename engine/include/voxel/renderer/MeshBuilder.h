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

/// Builds chunk meshes using naive face culling (one quad per visible face, no merging).
class MeshBuilder
{
  public:
    explicit MeshBuilder(const world::BlockRegistry& registry);

    /// Build a mesh for a chunk section using naive face culling.
    /// Each visible face produces one quad (width=1, height=1, AO=3).
    ///
    /// @param section The section to mesh (16x16x16 voxels).
    /// @param neighbors The 6 neighbor sections for boundary face culling.
    ///        Uses BlockFace ordering: [PosX, NegX, PosY, NegY, PosZ, NegZ].
    ///        nullptr = treat boundary as air (emit face).
    /// @return The generated mesh data.
    [[nodiscard]] ChunkMesh buildNaive(
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
