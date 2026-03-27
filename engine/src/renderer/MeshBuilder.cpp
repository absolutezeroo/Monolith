#include "voxel/renderer/MeshBuilder.h"

#include "voxel/renderer/AmbientOcclusion.h"
#include "voxel/renderer/ModelRegistry.h"
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

/// Pre-allocated workspace for greedy meshing to avoid per-call heap allocation (~44 KB total).
struct MeshWorkspace
{
    std::array<uint16_t, PAD_VOLUME> blockPad{};
    std::array<bool, OPACITY_PAD_VOLUME> opacityPad{};     // Full opacity (for AO)
    std::array<bool, OPACITY_PAD_VOLUME> cubicOpacityPad{}; // FullCube-only opacity (for face masks)
    std::array<uint16_t, 6 * S * S> faceMasks{};
    std::array<uint8_t, 6 * S * S * S> aoKeys{};           // Packed AO per visible face (4 corners, 2 bits each)
};


/// Stride-based pad access for eliminating per-element switch in inner loops.
/// For each face direction and a given slice value, provides base index + row/col strides
/// for linear traversal of the padded arrays without branching.
struct FaceSliceStrides
{
    int base;           // padded index for (row=0, col=0) in this slice
    int rowStride;      // padded index delta per row increment
    int colStride;      // padded index delta per col increment
    int neighborOffset; // padded index delta to reach neighbor in face direction
};

/// Compute stride-based access parameters for a given face direction and slice.
/// Both opacity and blockPad arrays use identical indexing: (py*18+pz)*18+px.
inline FaceSliceStrides getSliceStrides(uint8_t faceIdx, int slice)
{
    // Pad offset: row=0,col=0 maps to padded coord +1 on each axis.
    // Strides depend on which world axis maps to row vs col vs slice.
    switch (faceIdx)
    {
    case 0: // PosX: slice=X, row=Y, col=Z; neighbor +X
        return {PAD * PAD + PAD + (slice + 1), PAD * PAD, PAD, 1};
    case 1: // NegX: slice=X(rev), row=Y, col=Z; neighbor -X
        return {PAD * PAD + PAD + (S - slice), PAD * PAD, PAD, -1};
    case 2: // PosY: slice=Y, row=X, col=Z; neighbor +Y
        return {(slice + 1) * PAD * PAD + PAD + 1, 1, PAD, PAD * PAD};
    case 3: // NegY: slice=Y(rev), row=X, col=Z; neighbor -Y
        return {(S - slice) * PAD * PAD + PAD + 1, 1, PAD, -(PAD * PAD)};
    case 4: // PosZ: slice=Z, row=Y, col=X; neighbor +Z
        return {PAD * PAD + (slice + 1) * PAD + 1, PAD * PAD, 1, PAD};
    case 5: // NegZ: slice=Z(rev), row=Y, col=X; neighbor -Z
        return {PAD * PAD + (S - slice) * PAD + 1, PAD * PAD, 1, -PAD};
    default:
        return {0, 0, 0, 0};
    }
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
                blockPad[padIndex(x + 1, y + 1, z + 1)] = section.getBlock(x, y, z);
            }
        }
    }

    // PosX neighbor (face=0): x=0 slice from neighbor -> padded x=17
    if (neighbors[0] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int z = 0; z < S; ++z)
                blockPad[padIndex(17, y + 1, z + 1)] = neighbors[0]->getBlock(0, y, z);
    }
    // NegX neighbor (face=1): x=15 slice from neighbor -> padded x=0
    if (neighbors[1] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int z = 0; z < S; ++z)
                blockPad[padIndex(0, y + 1, z + 1)] = neighbors[1]->getBlock(S - 1, y, z);
    }
    // PosY neighbor (face=2): y=0 slice from neighbor -> padded y=17
    if (neighbors[2] != nullptr)
    {
        for (int z = 0; z < S; ++z)
            for (int x = 0; x < S; ++x)
                blockPad[padIndex(x + 1, 17, z + 1)] = neighbors[2]->getBlock(x, 0, z);
    }
    // NegY neighbor (face=3): y=15 slice from neighbor -> padded y=0
    if (neighbors[3] != nullptr)
    {
        for (int z = 0; z < S; ++z)
            for (int x = 0; x < S; ++x)
                blockPad[padIndex(x + 1, 0, z + 1)] = neighbors[3]->getBlock(x, S - 1, z);
    }
    // PosZ neighbor (face=4): z=0 slice from neighbor -> padded z=17
    if (neighbors[4] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                blockPad[padIndex(x + 1, y + 1, 17)] = neighbors[4]->getBlock(x, y, 0);
    }
    // NegZ neighbor (face=5): z=15 slice from neighbor -> padded z=0
    if (neighbors[5] != nullptr)
    {
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                blockPad[padIndex(x + 1, y + 1, 0)] = neighbors[5]->getBlock(x, y, S - 1);
    }
}

/// Build 18^3 padded opacity array that only marks FullCube opaque blocks as solid.
/// Non-cubic blocks (slab, cross, torch, etc.) are treated as transparent for face mask generation,
/// so cubic blocks always emit faces toward them and non-cubic blocks don't get face mask bits.
void buildCubicOpacityPad(
    std::array<bool, OPACITY_PAD_VOLUME>& cubicOpacity,
    const std::array<uint16_t, PAD_VOLUME>& blockPad,
    const world::BlockRegistry& registry)
{
    cubicOpacity.fill(false);

    for (int py = 0; py < PAD; ++py)
    {
        for (int pz = 0; pz < PAD; ++pz)
        {
            for (int px = 0; px < PAD; ++px)
            {
                int idx = padIndex(px, py, pz);
                uint16_t blockId = blockPad[idx];
                if (blockId != world::BLOCK_AIR)
                {
                    const world::BlockDefinition& def = registry.getBlockType(blockId);
                    cubicOpacity[idx] = !def.isTransparent && def.modelType == world::ModelType::FullCube;
                }
            }
        }
    }
}

/// Build face masks for one face direction using stride-based pad access (no per-element switch).
/// Produces 16 slices x 16 rows of uint16_t bitmasks.
/// outMasks[slice * 16 + row] = bitmask along col axis; bit=1 if face is visible.
void buildFaceMasks(
    uint8_t faceIdx,
    const std::array<bool, OPACITY_PAD_VOLUME>& opacity,
    uint16_t* outMasks)
{
    for (int slice = 0; slice < S; ++slice)
    {
        FaceSliceStrides st = getSliceStrides(faceIdx, slice);

        for (int row = 0; row < S; ++row)
        {
            uint16_t bits = 0;
            int idx = st.base + row * st.rowStride;

            for (int col = 0; col < S; ++col)
            {
                bool hereSolid = opacity[idx];
                bool neighborSolid = opacity[idx + st.neighborOffset];
                if (hereSolid && !neighborSolid)
                {
                    bits |= static_cast<uint16_t>(1u << col);
                }
                idx += st.colStride;
            }
            outMasks[slice * S + row] = bits;
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
    default: // Unreachable — all 6 faces covered.
        break;
    }
}

/// Pre-compute packed AO key for every visible face in a single face direction.
/// aoKeys[faceIdx * S³ + slice*S² + row*S + col] = (ao0<<6)|(ao1<<4)|(ao2<<2)|ao3.
/// Only computes for bits set in faceMasks; unset entries stay 0.
void buildAOKeys(
    uint8_t faceIdx,
    const uint16_t* faceMasks,
    const std::array<bool, OPACITY_PAD_VOLUME>& opacityPad,
    uint8_t* outKeys)
{
    for (int slice = 0; slice < S; ++slice)
    {
        for (int row = 0; row < S; ++row)
        {
            uint16_t bits = faceMasks[slice * S + row];
            while (bits != 0)
            {
                int col = std::countr_zero(static_cast<uint16_t>(bits));
                bits &= static_cast<uint16_t>(bits - 1); // clear lowest set bit

                int lx = 0;
                int ly = 0;
                int lz = 0;
                sliceToLocal(faceIdx, slice, row, col, lx, ly, lz);

                std::array<uint8_t, 4> ao = computeFaceAO(faceIdx, lx, ly, lz, opacityPad);
                outKeys[slice * S * S + row * S + col] =
                    static_cast<uint8_t>((ao[0] << 6) | (ao[1] << 4) | (ao[2] << 2) | ao[3]);
            }
        }
    }
}

/// Build face masks for transparent FullCube blocks in one face direction.
/// A transparent face is visible when its neighbor is air or a different transparent block.
void buildTransparentFaceMasks(
    uint8_t faceIdx,
    const std::array<uint16_t, PAD_VOLUME>& blockPad,
    const std::array<bool, OPACITY_PAD_VOLUME>& opacityPad,
    const world::BlockRegistry& registry,
    uint16_t* outMasks)
{
    for (int slice = 0; slice < S; ++slice)
    {
        FaceSliceStrides st = getSliceStrides(faceIdx, slice);

        for (int row = 0; row < S; ++row)
        {
            uint16_t bits = 0;
            int opIdx = st.base + row * st.rowStride;
            // blockPad uses identical layout: (py*18+pz)*18+px
            int bpIdx = opIdx;

            for (int col = 0; col < S; ++col)
            {
                uint16_t blockId = blockPad[bpIdx];
                if (blockId != world::BLOCK_AIR && !opacityPad[opIdx])
                {
                    // Current block is transparent — check it's FullCube.
                    const world::BlockDefinition& def = registry.getBlockType(blockId);
                    if (def.modelType == world::ModelType::FullCube)
                    {
                        // Neighbor must be air or transparent (not opaque).
                        bool neighborOpaque = opacityPad[opIdx + st.neighborOffset];
                        uint16_t neighborId = blockPad[bpIdx + st.neighborOffset];
                        if (!neighborOpaque && neighborId != blockId)
                        {
                            bits |= static_cast<uint16_t>(1u << col);
                        }
                    }
                }
                opIdx += st.colStride;
                bpIdx += st.colStride;
            }
            outMasks[slice * S + row] = bits;
        }
    }
}

/// Greedy merge one face direction: extends quads in width then height, constrained by
/// same block type AND same AO key. Emits packed quads into outQuads.
void greedyMergeFace(
    uint8_t faceIdx,
    uint16_t* sliceMasks,
    const uint8_t* aoKeys,
    const std::array<uint16_t, PAD_VOLUME>& blockPad,
    const world::BlockRegistry& registry,
    std::vector<uint64_t>& outQuads)
{
    for (int slice = 0; slice < S; ++slice)
    {
        FaceSliceStrides st = getSliceStrides(faceIdx, slice);
        uint16_t* masks = &sliceMasks[slice * S];
        const uint8_t* sliceAO = &aoKeys[slice * S * S];

        for (int row = 0; row < S; ++row)
        {
            uint16_t bits = masks[row];
            while (bits != 0)
            {
                int col = std::countr_zero(static_cast<uint16_t>(bits));
                int rowBase = st.base + row * st.rowStride;
                uint16_t type = blockPad[rowBase + col * st.colStride];
                uint8_t aoKey = sliceAO[row * S + col];

                // Extend width: consecutive same-type, same-AO set bits.
                int width = 1;
                uint16_t scanBits = static_cast<uint16_t>(bits >> (col + 1));
                while (scanBits & 1u)
                {
                    int nextCol = col + width;
                    if (blockPad[rowBase + nextCol * st.colStride] != type)
                    {
                        break;
                    }
                    if (sliceAO[row * S + nextCol] != aoKey)
                    {
                        break;
                    }
                    ++width;
                    scanBits >>= 1;
                }

                uint16_t widthMask = static_cast<uint16_t>(((1u << width) - 1) << col);

                // Extend height: subsequent rows with identical pattern, type, and AO.
                int height = 1;
                for (int r2 = row + 1; r2 < S; ++r2)
                {
                    if ((masks[r2] & widthMask) != widthMask)
                    {
                        break;
                    }

                    int r2Base = st.base + r2 * st.rowStride;
                    bool allMatch = true;
                    for (int c = col; c < col + width; ++c)
                    {
                        if (blockPad[r2Base + c * st.colStride] != type)
                        {
                            allMatch = false;
                            break;
                        }
                        if (sliceAO[r2 * S + c] != aoKey)
                        {
                            allMatch = false;
                            break;
                        }
                    }
                    if (!allMatch)
                    {
                        break;
                    }

                    masks[r2] &= static_cast<uint16_t>(~widthMask);
                    ++height;
                }

                bits &= static_cast<uint16_t>(~widthMask);

                // Map to local coordinates.
                int lx = 0;
                int ly = 0;
                int lz = 0;
                sliceToLocal(faceIdx, slice, row, col, lx, ly, lz);

                // Unpack AO from the key — all blocks in the merged region share the same AO.
                uint8_t ao0 = static_cast<uint8_t>((aoKey >> 6) & 0x3);
                uint8_t ao1 = static_cast<uint8_t>((aoKey >> 4) & 0x3);
                uint8_t ao2 = static_cast<uint8_t>((aoKey >> 2) & 0x3);
                uint8_t ao3 = static_cast<uint8_t>(aoKey & 0x3);
                std::array<uint8_t, 4> ao = {ao0, ao1, ao2, ao3};
                bool flip = shouldFlipQuad(ao);

                const world::BlockDefinition& blockDef = registry.getBlockType(type);
                uint64_t quad = packQuad(
                    static_cast<uint8_t>(lx),
                    static_cast<uint8_t>(ly),
                    static_cast<uint8_t>(lz),
                    type,
                    static_cast<BlockFace>(faceIdx),
                    static_cast<uint8_t>(width),
                    static_cast<uint8_t>(height),
                    ao0,
                    ao1,
                    ao2,
                    ao3,
                    flip,
                    blockDef.tintIndex,
                    blockDef.waving);
                outQuads.push_back(quad);
            }
        }
    }
}

/// Per-face mapping: for each AO corner index, which block in the merged quad provides that corner.
/// AO_CORNER_BLOCK[face][corner] = {row_uses_max, col_uses_max} where 0=min pos, 1=max pos.
/// Derived from analyzing AO_OFFSETS tangent axis signs per face/corner.
// clang-format off
constexpr int AO_CORNER_BLOCK[6][4][2] = {
    // PosX(0): row=Y, col=Z — c0(-Y,-Z), c1(+Y,-Z), c2(+Y,+Z), c3(-Y,+Z)
    {{0,0}, {1,0}, {1,1}, {0,1}},
    // NegX(1): row=Y, col=Z — c0(-Y,+Z), c1(+Y,+Z), c2(+Y,-Z), c3(-Y,-Z)
    {{0,1}, {1,1}, {1,0}, {0,0}},
    // PosY(2): row=X, col=Z — c0(-X,-Z), c1(+X,-Z), c2(+X,+Z), c3(-X,+Z)
    {{0,0}, {1,0}, {1,1}, {0,1}},
    // NegY(3): row=X, col=Z — c0(-X,-Z), c1(+X,-Z), c2(+X,+Z), c3(-X,+Z)
    {{0,0}, {1,0}, {1,1}, {0,1}},
    // PosZ(4): row=Y, col=X — c0(-X,-Y), c1(+X,-Y), c2(+X,+Y), c3(-X,+Y)
    {{0,0}, {0,1}, {1,1}, {1,0}},
    // NegZ(5): row=Y, col=X — c0(+X,-Y), c1(-X,-Y), c2(-X,+Y), c3(+X,+Y)
    {{0,1}, {0,0}, {1,0}, {1,1}},
};
// clang-format on

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

    // Opposite face direction lookup: PosX↔NegX, PosY↔NegY, PosZ↔NegZ.
    constexpr uint8_t OPPOSITE_FACE[6] = {1, 0, 3, 2, 5, 4};

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

                const world::BlockDefinition& blockDef = m_registry.getBlockType(blockId);

                // Skip non-cubic blocks — they are handled in buildNonCubicPass().
                if (blockDef.modelType != world::ModelType::FullCube)
                {
                    continue;
                }

                // Check each of the 6 faces.
                for (uint8_t f = 0; f < BLOCK_FACE_COUNT; ++f)
                {
                    BlockFace face = static_cast<BlockFace>(f);
                    uint16_t neighborId = getAdjacentBlock(section, neighbors, x, y, z, face);

                    // Emit face if neighbor is air, transparent, or doesn't fully cover this face.
                    bool shouldEmit = false;
                    if (neighborId == world::BLOCK_AIR)
                    {
                        shouldEmit = true;
                    }
                    else
                    {
                        const world::BlockDefinition& neighborDef = m_registry.getBlockType(neighborId);
                        if (neighborDef.isTransparent)
                        {
                            shouldEmit = true;
                        }
                        else if (neighborDef.modelType != world::ModelType::FullCube)
                        {
                            // Non-cubic neighbor: check if it fully covers the opposite face.
                            world::StateMap neighborState = m_registry.getStateValues(neighborId);
                            shouldEmit = !neighborDef.isFullFace(OPPOSITE_FACE[f], neighborState);
                        }
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
                            flip,
                            blockDef.tintIndex,
                            blockDef.waving);
                        mesh.quads.push_back(quad);
                    }
                }
            }
        }
    }

    mesh.quadCount = static_cast<uint32_t>(mesh.quads.size());

    // Second pass: generate model vertices for non-cubic blocks.
    buildNonCubicPass(section, neighbors, mesh);

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
    buildCubicOpacityPad(ws.cubicOpacityPad, ws.blockPad, m_registry);

    // Pass 1 — Opaque FullCube blocks: face masks from cubicOpacityPad, AO-aware greedy merge.
    for (uint8_t faceIdx = 0; faceIdx < BLOCK_FACE_COUNT; ++faceIdx)
    {
        uint16_t* masks = &ws.faceMasks[faceIdx * S * S];
        uint8_t* aoKeys = &ws.aoKeys[faceIdx * S * S * S];

        buildFaceMasks(faceIdx, ws.cubicOpacityPad, masks);
        buildAOKeys(faceIdx, masks, ws.opacityPad, aoKeys);
        greedyMergeFace(faceIdx, masks, aoKeys, ws.blockPad, m_registry, mesh.quads);
    }

    // Pass 2 — Transparent FullCube blocks: separate face masks, same AO-aware merge.
    for (uint8_t faceIdx = 0; faceIdx < BLOCK_FACE_COUNT; ++faceIdx)
    {
        uint16_t* masks = &ws.faceMasks[faceIdx * S * S];
        uint8_t* aoKeys = &ws.aoKeys[faceIdx * S * S * S];

        buildTransparentFaceMasks(faceIdx, ws.blockPad, ws.opacityPad, m_registry, masks);
        buildAOKeys(faceIdx, masks, ws.opacityPad, aoKeys);
        greedyMergeFace(faceIdx, masks, aoKeys, ws.blockPad, m_registry, mesh.quads);
    }

    mesh.quadCount = static_cast<uint32_t>(mesh.quads.size());

    // Non-cubic pass for greedy mesher too.
    buildNonCubicPass(section, neighbors, mesh);

    return mesh;
}

void MeshBuilder::buildNonCubicPass(
    const world::ChunkSection& section,
    const std::array<const world::ChunkSection*, 6>& neighbors,
    ChunkMesh& mesh) const
{
    constexpr int SIZE = world::ChunkSection::SIZE;

    for (int y = 0; y < SIZE; ++y)
    {
        for (int z = 0; z < SIZE; ++z)
        {
            for (int x = 0; x < SIZE; ++x)
            {
                uint16_t blockId = section.getBlock(x, y, z);
                if (blockId == world::BLOCK_AIR)
                {
                    continue;
                }

                const world::BlockDefinition& blockDef = m_registry.getBlockType(blockId);
                if (blockDef.modelType == world::ModelType::FullCube)
                {
                    continue;
                }

                // Compute face visibility mask: cull faces occluded by solid FullCube neighbors.
                uint8_t faceMask = 0;
                for (uint8_t f = 0; f < BLOCK_FACE_COUNT; ++f)
                {
                    uint16_t neighborId =
                        getAdjacentBlock(section, neighbors, x, y, z, static_cast<BlockFace>(f));
                    bool occluded = false;
                    if (neighborId != world::BLOCK_AIR)
                    {
                        const world::BlockDefinition& neighborDef = m_registry.getBlockType(neighborId);
                        occluded = !neighborDef.isTransparent && neighborDef.modelType == world::ModelType::FullCube;
                    }
                    if (!occluded)
                    {
                        faceMask |= static_cast<uint8_t>(1u << f);
                    }
                }

                world::StateMap state = m_registry.getStateValues(blockId);
                m_modelRegistry.getModelVertices(x, y, z, blockDef, state, blockId, faceMask, mesh.modelVertices);
            }
        }
    }

    mesh.modelVertexCount = static_cast<uint32_t>(mesh.modelVertices.size());
}

} // namespace voxel::renderer
