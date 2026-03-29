#pragma once

#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/ModelRegistry.h"

#include <array>

namespace voxel::world
{
class BlockRegistry;
class LightMap;
struct ChunkSection;
} // namespace voxel::world

namespace voxel::renderer
{

/// Builds chunk meshes using naive face culling or binary greedy meshing.
/// Handles both cubic (quad path) and non-cubic (ModelVertex path) blocks.
class MeshBuilder
{
  public:
    explicit MeshBuilder(const world::BlockRegistry& registry);

    /// Build a mesh for a chunk section using naive face culling.
    /// Each visible face produces one quad (width=1, height=1, per-vertex AO computed).
    /// Non-cubic blocks are emitted as ModelVertex data via a second pass.
    ///
    /// @param section The section to mesh (16x16x16 voxels).
    /// @param neighbors The 6 neighbor sections for boundary face culling.
    ///        Uses BlockFace ordering: [PosX, NegX, PosY, NegY, PosZ, NegZ].
    ///        nullptr = treat boundary as air (emit face).
    /// @param lightMap Optional light data for this section. nullptr = default light (sky=15, block=0).
    /// @param neighborLightMaps Optional light data for 6 neighbors. nullptr entries = open air (sky=15).
    /// @return The generated mesh data.
    [[nodiscard]] ChunkMesh buildNaive(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors,
        const world::LightMap* lightMap = nullptr,
        const std::array<const world::LightMap*, 6>& neighborLightMaps = {}) const;

    /// Build a mesh for a chunk section using binary greedy meshing.
    /// Merges coplanar adjacent faces of the same block type into larger quads.
    /// Non-cubic blocks are excluded from greedy merging and emitted via ModelVertex path.
    /// Same inputs/output as buildNaive() — callers can swap freely.
    ///
    /// @param section The section to mesh (16x16x16 voxels).
    /// @param neighbors The 6 neighbor sections for boundary face culling.
    ///        Uses BlockFace ordering: [PosX, NegX, PosY, NegY, PosZ, NegZ].
    ///        nullptr = treat boundary as air (emit face).
    /// @param lightMap Optional light data for this section. nullptr = default light (sky=15, block=0).
    /// @param neighborLightMaps Optional light data for 6 neighbors. nullptr entries = open air (sky=15).
    /// @return The generated mesh data with merged quads.
    [[nodiscard]] ChunkMesh buildGreedy(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors,
        const world::LightMap* lightMap = nullptr,
        const std::array<const world::LightMap*, 6>& neighborLightMaps = {}) const;

  private:
    const world::BlockRegistry& m_registry;
    ModelRegistry m_modelRegistry;

    /// Get the block ID adjacent to (x,y,z) in the given face direction.
    /// Returns the block from the neighbor section for boundary coordinates.
    [[nodiscard]] uint16_t getAdjacentBlock(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors,
        int x,
        int y,
        int z,
        BlockFace face) const;

    /// Build model vertices for all non-cubic blocks in the section.
    void buildNonCubicPass(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors,
        const world::LightMap* lightMap,
        ChunkMesh& mesh) const;
};

} // namespace voxel::renderer
