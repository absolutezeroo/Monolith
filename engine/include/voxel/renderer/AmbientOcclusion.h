#pragma once

#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

#include <array>
#include <cstdint>

namespace voxel::renderer
{

/// AO neighbor sampling offsets: AO_OFFSETS[face][corner][sample].
/// sample 0 = side1, sample 1 = side2, sample 2 = corner diagonal.
/// Each offset is {dx, dy, dz} relative to the face normal direction.
///
/// Face indices match BlockFace enum: PosX=0, NegX=1, PosY=2, NegY=3, PosZ=4, NegZ=5.
// clang-format off
inline constexpr int AO_OFFSETS[6][4][3][3] = {
    // PosX (face=0): tangent axes = Y, Z
    {
        {{ +1, -1,  0 }, { +1,  0, -1 }, { +1, -1, -1 }}, // corner 0
        {{ +1, +1,  0 }, { +1,  0, -1 }, { +1, +1, -1 }}, // corner 1
        {{ +1, +1,  0 }, { +1,  0, +1 }, { +1, +1, +1 }}, // corner 2
        {{ +1, -1,  0 }, { +1,  0, +1 }, { +1, -1, +1 }}, // corner 3
    },
    // NegX (face=1): tangent axes = Y, Z
    {
        {{ -1, -1,  0 }, { -1,  0, +1 }, { -1, -1, +1 }}, // corner 0
        {{ -1, +1,  0 }, { -1,  0, +1 }, { -1, +1, +1 }}, // corner 1
        {{ -1, +1,  0 }, { -1,  0, -1 }, { -1, +1, -1 }}, // corner 2
        {{ -1, -1,  0 }, { -1,  0, -1 }, { -1, -1, -1 }}, // corner 3
    },
    // PosY (face=2): tangent axes = X, Z
    {
        {{ -1, +1,  0 }, {  0, +1, -1 }, { -1, +1, -1 }}, // corner 0
        {{ +1, +1,  0 }, {  0, +1, -1 }, { +1, +1, -1 }}, // corner 1
        {{ +1, +1,  0 }, {  0, +1, +1 }, { +1, +1, +1 }}, // corner 2
        {{ -1, +1,  0 }, {  0, +1, +1 }, { -1, +1, +1 }}, // corner 3
    },
    // NegY (face=3): tangent axes = X, Z
    {
        {{ -1, -1,  0 }, {  0, -1, -1 }, { -1, -1, -1 }}, // corner 0
        {{ +1, -1,  0 }, {  0, -1, -1 }, { +1, -1, -1 }}, // corner 1
        {{ +1, -1,  0 }, {  0, -1, +1 }, { +1, -1, +1 }}, // corner 2
        {{ -1, -1,  0 }, {  0, -1, +1 }, { -1, -1, +1 }}, // corner 3
    },
    // PosZ (face=4): tangent axes = X, Y
    {
        {{ -1,  0, +1 }, {  0, -1, +1 }, { -1, -1, +1 }}, // corner 0
        {{ +1,  0, +1 }, {  0, -1, +1 }, { +1, -1, +1 }}, // corner 1
        {{ +1,  0, +1 }, {  0, +1, +1 }, { +1, +1, +1 }}, // corner 2
        {{ -1,  0, +1 }, {  0, +1, +1 }, { -1, +1, +1 }}, // corner 3
    },
    // NegZ (face=5): tangent axes = X, Y
    {
        {{ +1,  0, -1 }, {  0, -1, -1 }, { +1, -1, -1 }}, // corner 0
        {{ -1,  0, -1 }, {  0, -1, -1 }, { -1, -1, -1 }}, // corner 1
        {{ -1,  0, -1 }, {  0, +1, -1 }, { -1, +1, -1 }}, // corner 2
        {{ +1,  0, -1 }, {  0, +1, -1 }, { +1, +1, -1 }}, // corner 3
    },
};
// clang-format on

/// Compute per-vertex AO value (0 = full occlusion, 3 = no occlusion).
/// side1, side2: whether an opaque block is on each side of the vertex edge.
/// corner: whether an opaque block is at the diagonal.
/// Reference: 0fps.net ambient occlusion for Minecraft-like worlds.
inline constexpr int vertexAO(bool side1, bool corner, bool side2)
{
    if (side1 && side2)
    {
        return 0;
    }
    return 3 - static_cast<int>(side1) - static_cast<int>(side2) - static_cast<int>(corner);
}

/// Padded opacity array size: 18^3 for a 16^3 section with 1-block border.
inline constexpr int OPACITY_PAD_SIZE = 18;
inline constexpr int OPACITY_PAD_VOLUME = OPACITY_PAD_SIZE * OPACITY_PAD_SIZE * OPACITY_PAD_SIZE;

/// Convert padded coordinates to flat index. Padded coords: section local (0-15) maps to padded (1-16).
inline constexpr int padIndex(int px, int py, int pz)
{
    return (py * OPACITY_PAD_SIZE + pz) * OPACITY_PAD_SIZE + px;
}

/// Build 18x18x18 padded opacity array from a section and its 6 face neighbors.
/// Center 16^3 is filled from section; 1-block borders from face neighbors (nullptr = air).
/// Edge/corner cells without diagonal neighbor data default to false (air = no occlusion).
inline void buildOpacityPad(
    std::array<bool, OPACITY_PAD_VOLUME>& opacity,
    const world::ChunkSection& section,
    const std::array<const world::ChunkSection*, 6>& neighbors,
    const world::BlockRegistry& registry)
{
    opacity.fill(false);

    constexpr int S = world::ChunkSection::SIZE; // 16

    // Fill center 16^3: padded coords (1-16).
    for (int y = 0; y < S; ++y)
    {
        for (int z = 0; z < S; ++z)
        {
            for (int x = 0; x < S; ++x)
            {
                uint16_t blockId = section.getBlock(x, y, z);
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlock(blockId);
                    opacity[padIndex(x + 1, y + 1, z + 1)] = !def.isTransparent;
                }
            }
        }
    }

    // PosX neighbor (face=0): copy x=0 slice from neighbor as padded x=17.
    if (neighbors[0] != nullptr)
    {
        for (int y = 0; y < S; ++y)
        {
            for (int z = 0; z < S; ++z)
            {
                uint16_t blockId = neighbors[0]->getBlock(0, y, z);
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlock(blockId);
                    opacity[padIndex(17, y + 1, z + 1)] = !def.isTransparent;
                }
            }
        }
    }

    // NegX neighbor (face=1): copy x=15 slice from neighbor as padded x=0.
    if (neighbors[1] != nullptr)
    {
        for (int y = 0; y < S; ++y)
        {
            for (int z = 0; z < S; ++z)
            {
                uint16_t blockId = neighbors[1]->getBlock(S - 1, y, z);
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlock(blockId);
                    opacity[padIndex(0, y + 1, z + 1)] = !def.isTransparent;
                }
            }
        }
    }

    // PosY neighbor (face=2): copy y=0 slice from neighbor as padded y=17.
    if (neighbors[2] != nullptr)
    {
        for (int z = 0; z < S; ++z)
        {
            for (int x = 0; x < S; ++x)
            {
                uint16_t blockId = neighbors[2]->getBlock(x, 0, z);
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlock(blockId);
                    opacity[padIndex(x + 1, 17, z + 1)] = !def.isTransparent;
                }
            }
        }
    }

    // NegY neighbor (face=3): copy y=15 slice from neighbor as padded y=0.
    if (neighbors[3] != nullptr)
    {
        for (int z = 0; z < S; ++z)
        {
            for (int x = 0; x < S; ++x)
            {
                uint16_t blockId = neighbors[3]->getBlock(x, S - 1, z);
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlock(blockId);
                    opacity[padIndex(x + 1, 0, z + 1)] = !def.isTransparent;
                }
            }
        }
    }

    // PosZ neighbor (face=4): copy z=0 slice from neighbor as padded z=17.
    if (neighbors[4] != nullptr)
    {
        for (int y = 0; y < S; ++y)
        {
            for (int x = 0; x < S; ++x)
            {
                uint16_t blockId = neighbors[4]->getBlock(x, y, 0);
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlock(blockId);
                    opacity[padIndex(x + 1, y + 1, 17)] = !def.isTransparent;
                }
            }
        }
    }

    // NegZ neighbor (face=5): copy z=15 slice from neighbor as padded z=0.
    if (neighbors[5] != nullptr)
    {
        for (int y = 0; y < S; ++y)
        {
            for (int x = 0; x < S; ++x)
            {
                uint16_t blockId = neighbors[5]->getBlock(x, y, S - 1);
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlock(blockId);
                    opacity[padIndex(x + 1, y + 1, 0)] = !def.isTransparent;
                }
            }
        }
    }
}

/// Compute AO for all 4 corners of a face, using the padded opacity array.
/// Returns array of 4 AO values (0-3), one per corner.
inline std::array<uint8_t, 4> computeFaceAO(
    uint8_t face,
    int x,
    int y,
    int z,
    const std::array<bool, OPACITY_PAD_VOLUME>& opacity)
{
    std::array<uint8_t, 4> ao{};

    // Padded coordinates for the block.
    int px = x + 1;
    int py = y + 1;
    int pz = z + 1;

    for (int corner = 0; corner < 4; ++corner)
    {
        const auto& offsets = AO_OFFSETS[face][corner];

        bool side1 = opacity[padIndex(px + offsets[0][0], py + offsets[0][1], pz + offsets[0][2])];
        bool side2 = opacity[padIndex(px + offsets[1][0], py + offsets[1][1], pz + offsets[1][2])];
        bool diag = opacity[padIndex(px + offsets[2][0], py + offsets[2][1], pz + offsets[2][2])];

        ao[corner] = static_cast<uint8_t>(vertexAO(side1, diag, side2));
    }

    return ao;
}

/// Determine if a quad's triangulation should be flipped to reduce AO interpolation artifacts.
/// Uses the canonical 0fps.net sum-comparison formula.
inline constexpr bool shouldFlipQuad(const std::array<uint8_t, 4>& ao)
{
    return (ao[0] + ao[3]) > (ao[1] + ao[2]);
}

} // namespace voxel::renderer
