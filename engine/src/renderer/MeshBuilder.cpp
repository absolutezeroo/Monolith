#include "voxel/renderer/MeshBuilder.h"

#include "voxel/renderer/AmbientOcclusion.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

#include <array>
#include <bit>
#include <cstdint>

namespace voxel::renderer
{

namespace
{

constexpr int PAD = 18;
constexpr int PAD_VOLUME = PAD * PAD * PAD;
constexpr int S = world::ChunkSection::SIZE; // 16

/// Pre-allocated workspace for greedy meshing to avoid per-call heap allocation (~20 KB total).
struct MeshWorkspace
{
    std::array<uint16_t, PAD_VOLUME> blockPad{};
    std::array<bool, OPACITY_PAD_VOLUME> opacityPad{};
    std::array<uint16_t, 6 * S * S> faceMasks{};
    std::array<uint16_t, S * S> sliceBlockTypes{};
};

inline int blockPadIndex(int px, int py, int pz)
{
    return (py * PAD + pz) * PAD + px;
}

/// Build 18^3 padded block ID array from section + neighbors.
void buildBlockPad(
    std::array<uint16_t, PAD_VOLUME>& blockPad,
    const world::ChunkSection& section,
    const std::array<const world::ChunkSection*, 6>& neighbors)
{
    blockPad.fill(world::BLOCK_AIR);

    // Center 16^3
    for (int y = 0; y < S; ++y)
    {
        for (int z = 0; z < S; ++z)
        {
            for (int x = 0; x < S; ++x)
            {
                blockPad[blockPadIndex(x + 1, y + 1, z + 1)] = section.getBlock(x, y, z);
            }
        }
    }

    // PosX neighbor (face=0): x=0 slice from neighbor -> padded x=17
    if (neighbors[0] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int z = 0; z < S; ++z)
                blockPad[blockPadIndex(17, y + 1, z + 1)] = neighbors[0]->getBlock(0, y, z);
    }
    // NegX neighbor (face=1): x=15 slice from neighbor -> padded x=0
    if (neighbors[1] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int z = 0; z < S; ++z)
                blockPad[blockPadIndex(0, y + 1, z + 1)] = neighbors[1]->getBlock(S - 1, y, z);
    }
    // PosY neighbor (face=2): y=0 slice from neighbor -> padded y=17
    if (neighbors[2] != nullptr)
    {
        for (int z = 0; z < S; ++z)
            for (int x = 0; x < S; ++x)
                blockPad[blockPadIndex(x + 1, 17, z + 1)] = neighbors[2]->getBlock(x, 0, z);
    }
    // NegY neighbor (face=3): y=15 slice from neighbor -> padded y=0
    if (neighbors[3] != nullptr)
    {
        for (int z = 0; z < S; ++z)
            for (int x = 0; x < S; ++x)
                blockPad[blockPadIndex(x + 1, 0, z + 1)] = neighbors[3]->getBlock(x, S - 1, z);
    }
    // PosZ neighbor (face=4): z=0 slice from neighbor -> padded z=17
    if (neighbors[4] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                blockPad[blockPadIndex(x + 1, y + 1, 17)] = neighbors[4]->getBlock(x, y, 0);
    }
    // NegZ neighbor (face=5): z=15 slice from neighbor -> padded z=0
    if (neighbors[5] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                blockPad[blockPadIndex(x + 1, y + 1, 0)] = neighbors[5]->getBlock(x, y, S - 1);
    }
}

/// Build face masks for one face direction. Produces 16 slices x 16 rows of uint16_t bitmasks.
/// faceMasks output: faceMasks[slice * 16 + row] = bitmask along col axis.
void buildFaceMasks(
    uint8_t faceIdx,
    const std::array<bool, OPACITY_PAD_VOLUME>& opacity,
    uint16_t* outMasks)
{
    // For each face: iterate slices perpendicular to normal, rows and cols in the tangent plane.
    // Bit is set if: block here is opaque AND neighbor in face direction is not opaque.
    //
    // Axis mapping (slice, row, col) -> padded (px, py, pz):
    //   PosX(0): slice=x, row=y, col=z, neighbor at x+1
    //   NegX(1): slice=x, row=y, col=z, neighbor at x-1 (slice reversed)
    //   PosY(2): slice=y, row=x, col=z, neighbor at y+1
    //   NegY(3): slice=y, row=x, col=z, neighbor at y-1 (slice reversed)
    //   PosZ(4): slice=z, row=y, col=x, neighbor at z+1
    //   NegZ(5): slice=z, row=y, col=x, neighbor at z-1 (slice reversed)

    for (int slice = 0; slice < S; ++slice)
    {
        for (int row = 0; row < S; ++row)
        {
            uint16_t bits = 0;
            for (int col = 0; col < S; ++col)
            {
                int px = 0;
                int py = 0;
                int pz = 0;
                int npx = 0;
                int npy = 0;
                int npz = 0;

                switch (faceIdx)
                {
                case 0: // PosX
                    px = slice + 1;
                    py = row + 1;
                    pz = col + 1;
                    npx = px + 1;
                    npy = py;
                    npz = pz;
                    break;
                case 1: // NegX
                    px = (S - 1 - slice) + 1;
                    py = row + 1;
                    pz = col + 1;
                    npx = px - 1;
                    npy = py;
                    npz = pz;
                    break;
                case 2: // PosY
                    px = row + 1;
                    py = slice + 1;
                    pz = col + 1;
                    npx = px;
                    npy = py + 1;
                    npz = pz;
                    break;
                case 3: // NegY
                    px = row + 1;
                    py = (S - 1 - slice) + 1;
                    pz = col + 1;
                    npx = px;
                    npy = py - 1;
                    npz = pz;
                    break;
                case 4: // PosZ
                    px = col + 1;
                    py = row + 1;
                    pz = slice + 1;
                    npx = px;
                    npy = py;
                    npz = pz + 1;
                    break;
                case 5: // NegZ
                    px = col + 1;
                    py = row + 1;
                    pz = (S - 1 - slice) + 1;
                    npx = px;
                    npy = py;
                    npz = pz - 1;
                    break;
                }

                bool hereSolid = opacity[padIndex(px, py, pz)];
                bool neighborSolid = opacity[padIndex(npx, npy, npz)];
                if (hereSolid && !neighborSolid)
                {
                    bits |= static_cast<uint16_t>(1u << col);
                }
            }
            outMasks[slice * S + row] = bits;
        }
    }
}

/// Build the block type cache for one slice of one face direction.
/// sliceTypes[row * 16 + col] = blockId at that position.
void buildSliceBlockTypes(
    uint8_t faceIdx,
    int slice,
    const std::array<uint16_t, PAD_VOLUME>& blockPad,
    uint16_t* outTypes)
{
    for (int row = 0; row < S; ++row)
    {
        for (int col = 0; col < S; ++col)
        {
            int px = 0;
            int py = 0;
            int pz = 0;

            switch (faceIdx)
            {
            case 0: // PosX
                px = slice + 1;
                py = row + 1;
                pz = col + 1;
                break;
            case 1: // NegX
                px = (S - 1 - slice) + 1;
                py = row + 1;
                pz = col + 1;
                break;
            case 2: // PosY
                px = row + 1;
                py = slice + 1;
                pz = col + 1;
                break;
            case 3: // NegY
                px = row + 1;
                py = (S - 1 - slice) + 1;
                pz = col + 1;
                break;
            case 4: // PosZ
                px = col + 1;
                py = row + 1;
                pz = slice + 1;
                break;
            case 5: // NegZ
                px = col + 1;
                py = row + 1;
                pz = (S - 1 - slice) + 1;
                break;
            }

            outTypes[row * S + col] = blockPad[blockPadIndex(px, py, pz)];
        }
    }
}

/// Map (faceIdx, slice, row, col) back to local section coordinates (x, y, z).
void sliceToLocal(uint8_t faceIdx, int slice, int row, int col, int& x, int& y, int& z)
{
    switch (faceIdx)
    {
    case 0: // PosX
        x = slice;
        y = row;
        z = col;
        break;
    case 1: // NegX
        x = S - 1 - slice;
        y = row;
        z = col;
        break;
    case 2: // PosY
        x = row;
        y = slice;
        z = col;
        break;
    case 3: // NegY
        x = row;
        y = S - 1 - slice;
        z = col;
        break;
    case 4: // PosZ
        x = col;
        y = row;
        z = slice;
        break;
    case 5: // NegZ
        x = col;
        y = row;
        z = S - 1 - slice;
        break;
    }
}

} // anonymous namespace

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

ChunkMesh MeshBuilder::buildGreedy(
    const world::ChunkSection& section,
    const std::array<const world::ChunkSection*, 6>& neighbors) const
{
    ChunkMesh mesh;

    if (section.isEmpty())
    {
        return mesh;
    }

    constexpr int ESTIMATED_QUADS = 2048;
    mesh.quads.reserve(ESTIMATED_QUADS);

    // Phase 0: Build padded data (block IDs + opacity).
    MeshWorkspace ws;
    buildBlockPad(ws.blockPad, section, neighbors);
    buildOpacityPad(ws.opacityPad, section, neighbors, m_registry);

    // Phase 1 & 2: For each face direction, build face masks then greedy merge per slice.
    for (uint8_t faceIdx = 0; faceIdx < BLOCK_FACE_COUNT; ++faceIdx)
    {
        uint16_t* masks = &ws.faceMasks[faceIdx * S * S];
        buildFaceMasks(faceIdx, ws.opacityPad, masks);

        for (int slice = 0; slice < S; ++slice)
        {
            // Build block type cache for this slice.
            buildSliceBlockTypes(faceIdx, slice, ws.blockPad, ws.sliceBlockTypes.data());

            uint16_t* sliceMasks = &masks[slice * S];

            for (int row = 0; row < S; ++row)
            {
                uint16_t bits = sliceMasks[row];
                while (bits != 0)
                {
                    int col = std::countr_zero(static_cast<uint16_t>(bits));
                    uint16_t type = ws.sliceBlockTypes[row * S + col];

                    // Extend width: consecutive same-type set bits.
                    int width = 1;
                    uint16_t scanBits = static_cast<uint16_t>(bits >> (col + 1));
                    while (scanBits & 1u)
                    {
                        if (ws.sliceBlockTypes[row * S + col + width] != type)
                        {
                            break;
                        }
                        ++width;
                        scanBits >>= 1;
                    }

                    uint16_t widthMask = static_cast<uint16_t>(((1u << width) - 1) << col);

                    // Extend height: subsequent rows with identical pattern and type.
                    int height = 1;
                    for (int r2 = row + 1; r2 < S; ++r2)
                    {
                        if ((sliceMasks[r2] & widthMask) != widthMask)
                        {
                            break;
                        }

                        bool allSameType = true;
                        for (int c = col; c < col + width; ++c)
                        {
                            if (ws.sliceBlockTypes[r2 * S + c] != type)
                            {
                                allSameType = false;
                                break;
                            }
                        }
                        if (!allSameType)
                        {
                            break;
                        }

                        sliceMasks[r2] &= static_cast<uint16_t>(~widthMask);
                        ++height;
                    }

                    bits &= static_cast<uint16_t>(~widthMask);

                    // Map to local coordinates and emit quad.
                    int lx = 0;
                    int ly = 0;
                    int lz = 0;
                    sliceToLocal(faceIdx, slice, row, col, lx, ly, lz);

                    // Compute AO at the 4 physical corners of the merged quad.
                    // Corner positions depend on face direction's row/col axes.
                    // We sample AO at the 4 extremes of the merged rectangle.
                    int cornerRow[4] = {row, row, row + height, row + height};
                    int cornerCol[4] = {col, col + width, col + width, col};

                    std::array<uint8_t, 4> ao{};
                    for (int ci = 0; ci < 4; ++ci)
                    {
                        int cx = 0;
                        int cy = 0;
                        int cz = 0;
                        sliceToLocal(faceIdx, slice, cornerRow[ci], cornerCol[ci], cx, cy, cz);
                        ao[ci] = computeFaceAO(faceIdx, cx, cy, cz, ws.opacityPad)[ci];
                    }

                    bool flip = shouldFlipQuad(ao);

                    uint64_t quad = packQuad(
                        static_cast<uint8_t>(lx),
                        static_cast<uint8_t>(ly),
                        static_cast<uint8_t>(lz),
                        type,
                        static_cast<BlockFace>(faceIdx),
                        static_cast<uint8_t>(width),
                        static_cast<uint8_t>(height),
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

    mesh.quadCount = static_cast<uint32_t>(mesh.quads.size());
    return mesh;
}

} // namespace voxel::renderer
