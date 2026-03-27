#pragma once

#include "voxel/renderer/ChunkMesh.h"
#include "voxel/world/Block.h"

#include <glm/glm.hpp>

#include <vector>

namespace voxel::renderer
{

/// Geometry generators for non-cubic block models.
/// Each generator produces ModelVertex data for a specific ModelType.
class ModelMesher
{
  public:
    /// Generate slab geometry — a half-height box.
    /// half=bottom: box (0,0,0)→(1,0.5,1). half=top: box (0,0.5,0)→(1,1,1).
    /// @param x Block X position within section (0-15).
    /// @param y Block Y position within section (0-15).
    /// @param z Block Z position within section (0-15).
    /// @param blockDef Block definition for texture/state info.
    /// @param state Current state values (needs "half" property).
    /// @param stateId Actual block state ID (not base — may differ for state variants).
    /// @param ao Ambient occlusion value for all vertices (0-3).
    /// @param faceMask Bitmask of visible faces (bit i = face i, matching BlockFace enum).
    static void generateSlab(
        int x,
        int y,
        int z,
        const world::BlockDefinition& blockDef,
        const world::StateMap& state,
        uint16_t stateId,
        uint8_t ao,
        uint8_t faceMask,
        std::vector<ModelVertex>& outVertices);

    /// Generate cross geometry — two diagonal quads through the block center.
    /// Used for flowers, tall grass, etc. Both quads are double-sided.
    /// @param stateId Actual block state ID.
    static void generateCross(
        int x,
        int y,
        int z,
        const world::BlockDefinition& blockDef,
        uint16_t stateId,
        std::vector<ModelVertex>& outVertices);

    /// Generate torch geometry — a thin vertical box.
    /// Standing torch: centered (7/16 to 9/16 XZ, 0 to 10/16 Y).
    /// Wall torch: angled toward attached wall face.
    /// @param stateId Actual block state ID.
    /// @param faceMask Bitmask of visible faces (bit i = face i, matching BlockFace enum).
    static void generateTorch(
        int x,
        int y,
        int z,
        const world::BlockDefinition& blockDef,
        const world::StateMap& state,
        uint16_t stateId,
        uint8_t faceMask,
        std::vector<ModelVertex>& outVertices);

  private:
    /// Emit a quad (4 vertices, 2 triangles) as 6 individual vertices for triangle list rendering.
    static void emitQuad(
        const glm::vec3& v0,
        const glm::vec3& v1,
        const glm::vec3& v2,
        const glm::vec3& v3,
        const glm::vec3& normal,
        uint16_t blockStateId,
        uint8_t ao,
        uint8_t flags,
        std::vector<ModelVertex>& outVertices);

    /// Emit a box, emitting only faces whose bit is set in faceMask.
    /// faceMask bit layout matches BlockFace enum: 0=PosX, 1=NegX, 2=PosY, 3=NegY, 4=PosZ, 5=NegZ.
    static void emitBox(
        const glm::vec3& offset,
        const glm::vec3& minCorner,
        const glm::vec3& maxCorner,
        uint16_t blockStateId,
        uint8_t ao,
        uint8_t flags,
        uint8_t faceMask,
        std::vector<ModelVertex>& outVertices);
};

} // namespace voxel::renderer
