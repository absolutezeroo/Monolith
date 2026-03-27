#include "voxel/renderer/MeshBuilder.h"

#include "voxel/renderer/AmbientOcclusion.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

namespace voxel::renderer
{

MeshBuilder::MeshBuilder(const world::BlockRegistry& registry)
    : m_registry(registry)
{
}

ChunkMesh MeshBuilder::buildNaive(
    const world::ChunkSection& section,
    const std::array<const world::ChunkSection*, 6>& neighbors) const
{
    ChunkMesh mesh;

    // Fast-path: empty section produces no quads.
    if (section.isEmpty())
    {
        return mesh;
    }

    // Worst case: every block exposes all 6 faces = 16*16*16*6 = 24576 quads.
    // Typical terrain is ~30-50% of that. Reserve a reasonable estimate.
    constexpr int ESTIMATED_QUADS = 8192;
    mesh.quads.reserve(ESTIMATED_QUADS);

    constexpr int SIZE = world::ChunkSection::SIZE;

    // Build padded 18^3 opacity array for AO sampling across section boundaries.
    std::array<bool, OPACITY_PAD_VOLUME> opacity{};
    buildOpacityPad(opacity, section, neighbors, m_registry);

    // Iterate in Y-Z-X order for cache-friendly access (matches flat array layout y*256 + z*16 + x).
    for (int y = 0; y < SIZE; ++y)
    {
        for (int z = 0; z < SIZE; ++z)
        {
            for (int x = 0; x < SIZE; ++x)
            {
                uint16_t blockId = section.getBlock(x, y, z);

                // Skip air blocks — no faces to emit.
                if (blockId == world::BLOCK_AIR)
                {
                    continue;
                }

                // Check each of the 6 faces.
                for (uint8_t f = 0; f < BLOCK_FACE_COUNT; ++f)
                {
                    BlockFace face = static_cast<BlockFace>(f);
                    uint16_t neighborId = getAdjacentBlock(section, neighbors, x, y, z, face);

                    // Emit face if neighbor is air or transparent.
                    bool shouldEmit = false;
                    if (neighborId == world::BLOCK_AIR)
                    {
                        shouldEmit = true;
                    }
                    else
                    {
                        const world::BlockDefinition& neighborDef = m_registry.getBlock(neighborId);
                        shouldEmit = neighborDef.isTransparent;
                    }

                    if (shouldEmit)
                    {
                        std::array<uint8_t, 4> ao = computeFaceAO(f, x, y, z, opacity);
                        bool flip = shouldFlipQuad(ao);

                        uint64_t quad = packQuad(
                            static_cast<uint8_t>(x),
                            static_cast<uint8_t>(y),
                            static_cast<uint8_t>(z),
                            blockId,
                            face,
                            1,
                            1,
                            ao[0],
                            ao[1],
                            ao[2],
                            ao[3],
                            flip);
                        mesh.quads.push_back(quad);
                    }
                }
            }
        }
    }

    mesh.quadCount = static_cast<uint32_t>(mesh.quads.size());
    return mesh;
}

uint16_t MeshBuilder::getAdjacentBlock(
    const world::ChunkSection& section,
    const std::array<const world::ChunkSection*, 6>& neighbors,
    int x,
    int y,
    int z,
    BlockFace face) const
{
    constexpr int MAX_COORD = world::ChunkSection::SIZE - 1; // 15

    switch (face)
    {
    case BlockFace::PosX:
        if (x < MAX_COORD)
        {
            return section.getBlock(x + 1, y, z);
        }
        return neighbors[0] ? neighbors[0]->getBlock(0, y, z) : world::BLOCK_AIR;

    case BlockFace::NegX:
        if (x > 0)
        {
            return section.getBlock(x - 1, y, z);
        }
        return neighbors[1] ? neighbors[1]->getBlock(MAX_COORD, y, z) : world::BLOCK_AIR;

    case BlockFace::PosY:
        if (y < MAX_COORD)
        {
            return section.getBlock(x, y + 1, z);
        }
        return neighbors[2] ? neighbors[2]->getBlock(x, 0, z) : world::BLOCK_AIR;

    case BlockFace::NegY:
        if (y > 0)
        {
            return section.getBlock(x, y - 1, z);
        }
        return neighbors[3] ? neighbors[3]->getBlock(x, MAX_COORD, z) : world::BLOCK_AIR;

    case BlockFace::PosZ:
        if (z < MAX_COORD)
        {
            return section.getBlock(x, y, z + 1);
        }
        return neighbors[4] ? neighbors[4]->getBlock(x, y, 0) : world::BLOCK_AIR;

    case BlockFace::NegZ:
        if (z > 0)
        {
            return section.getBlock(x, y, z - 1);
        }
        return neighbors[5] ? neighbors[5]->getBlock(x, y, MAX_COORD) : world::BLOCK_AIR;
    }

    // Unreachable — all 6 faces covered.
    return world::BLOCK_AIR;
}

} // namespace voxel::renderer
