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
    /// @param ao Ambient occlusion value for all vertices (0-3).
    static void generateSlab(
        int x,
        int y,
        int z,
        const world::BlockDefinition& blockDef,
        const world::StateMap& state,
        uint8_t ao,
        std::vector<ModelVertex>& outVertices);

    /// Generate cross geometry — two diagonal quads through the block center.
    /// Used for flowers, tall grass, etc. Both quads are double-sided.
    static void generateCross(
        int x,
        int y,
        int z,
        const world::BlockDefinition& blockDef,
        std::vector<ModelVertex>& outVertices);

    /// Generate torch geometry — a thin vertical box.
    /// Standing torch: centered (7/16 to 9/16 XZ, 0 to 10/16 Y).
    /// Wall torch: angled toward attached wall face.
    static void generateTorch(
        int x,
        int y,
        int z,
        const world::BlockDefinition& blockDef,
        const world::StateMap& state,
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

    /// Emit a box (6 faces × 6 vertices = 36 vertices) with given bounds.
    static void emitBox(
        const glm::vec3& offset,
        const glm::vec3& minCorner,
        const glm::vec3& maxCorner,
        uint16_t blockStateId,
        uint8_t ao,
        uint8_t flags,
        std::vector<ModelVertex>& outVertices);
};

} // namespace voxel::renderer
