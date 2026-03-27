#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace voxel::renderer
{

/// Face direction for block faces. Ordering matches neighbor array convention.
enum class BlockFace : uint8_t
{
    PosX = 0, // +X
    NegX = 1, // -X
    PosY = 2, // +Y (up)
    NegY = 3, // -Y (down)
    PosZ = 4, // +Z
    NegZ = 5, // -Z
};

static constexpr uint8_t BLOCK_FACE_COUNT = 6;

/// 64-bit packed quad format.
///
/// Bit layout:
///   [0:5]   X position (0-63)
///   [6:11]  Y position (0-63)
///   [12:17] Z position (0-63)
///   [18:23] Width - 1  (0-63)
///   [24:29] Height - 1 (0-63)
///   [30:45] Block state ID (0-65535)
///   [46:48] Face direction (0-5)
///   [49:50] AO corner 0 (0-3)
///   [51:52] AO corner 1 (0-3)
///   [53:54] AO corner 2 (0-3)
///   [55:56] AO corner 3 (0-3)
///   [57]    Quad diagonal flip
///   [58]    Is non-cubic model (Story 5.4)
///   [59:60] Tint index (Story 5.5)
///   [61]    Waving flag (Story 5.5)
///   [62:63] Reserved

/// Pack a quad into the 64-bit format. Width and height default to 1 (no merge).
/// AO corners default to 3 (no occlusion). All other future fields default to 0.
inline constexpr uint64_t packQuad(
    uint8_t x,
    uint8_t y,
    uint8_t z,
    uint16_t blockStateId,
    BlockFace face,
    uint8_t width = 1,
    uint8_t height = 1,
    uint8_t ao0 = 3,
    uint8_t ao1 = 3,
    uint8_t ao2 = 3,
    uint8_t ao3 = 3,
    bool flip = false)
{
    uint64_t q = 0;
    q |= static_cast<uint64_t>(x & 0x3F);
    q |= static_cast<uint64_t>(y & 0x3F) << 6;
    q |= static_cast<uint64_t>(z & 0x3F) << 12;
    q |= static_cast<uint64_t>((width - 1) & 0x3F) << 18;
    q |= static_cast<uint64_t>((height - 1) & 0x3F) << 24;
    q |= static_cast<uint64_t>(blockStateId & 0xFFFF) << 30;
    q |= static_cast<uint64_t>(static_cast<uint8_t>(face) & 0x7) << 46;
    q |= static_cast<uint64_t>(ao0 & 0x3) << 49;
    q |= static_cast<uint64_t>(ao1 & 0x3) << 51;
    q |= static_cast<uint64_t>(ao2 & 0x3) << 53;
    q |= static_cast<uint64_t>(ao3 & 0x3) << 55;
    q |= static_cast<uint64_t>(flip ? 1 : 0) << 57;
    return q;
}

/// Unpack X position from a packed quad.
inline constexpr uint8_t unpackX(uint64_t quad) { return static_cast<uint8_t>(quad & 0x3F); }

/// Unpack Y position from a packed quad.
inline constexpr uint8_t unpackY(uint64_t quad) { return static_cast<uint8_t>((quad >> 6) & 0x3F); }

/// Unpack Z position from a packed quad.
inline constexpr uint8_t unpackZ(uint64_t quad) { return static_cast<uint8_t>((quad >> 12) & 0x3F); }

/// Unpack width (stored as width-1, so add 1).
inline constexpr uint8_t unpackWidth(uint64_t quad) { return static_cast<uint8_t>(((quad >> 18) & 0x3F) + 1); }

/// Unpack height (stored as height-1, so add 1).
inline constexpr uint8_t unpackHeight(uint64_t quad) { return static_cast<uint8_t>(((quad >> 24) & 0x3F) + 1); }

/// Unpack block state ID.
inline constexpr uint16_t unpackBlockStateId(uint64_t quad) { return static_cast<uint16_t>((quad >> 30) & 0xFFFF); }

/// Unpack face direction.
inline constexpr BlockFace unpackFace(uint64_t quad)
{
    return static_cast<BlockFace>((quad >> 46) & 0x7);
}

/// Unpack all 4 AO corner values (0-3 each).
inline constexpr std::array<uint8_t, 4> unpackAO(uint64_t quad)
{
    return {
        static_cast<uint8_t>((quad >> 49) & 0x3),
        static_cast<uint8_t>((quad >> 51) & 0x3),
        static_cast<uint8_t>((quad >> 53) & 0x3),
        static_cast<uint8_t>((quad >> 55) & 0x3),
    };
}

/// Unpack quad diagonal flip flag.
inline constexpr bool unpackFlip(uint64_t quad) { return ((quad >> 57) & 0x1) != 0; }

/// Vertex format for non-cubic block models (slabs, crosses, torches, etc.).
/// Used alongside the packed quad buffer for FullCube blocks.
struct ModelVertex
{
    glm::vec3 position; // World-relative position within chunk (0-16 range)
    glm::vec3 normal;   // Face normal for lighting
    glm::vec2 uv;       // Texture coordinates (0-1)
    uint16_t blockStateId = 0;
    uint8_t ao = 3;     // Ambient occlusion (0-3, same scale as quad AO)
    uint8_t flags = 0;  // Bit 0: tint index LSB, Bits 1-2: waving type
};

static_assert(sizeof(ModelVertex) == 36, "ModelVertex must be 36 bytes for GPU upload");

/// Mesh data for a single chunk section.
struct ChunkMesh
{
    std::vector<uint64_t> quads;
    uint32_t quadCount = 0;

    std::vector<ModelVertex> modelVertices;
    uint32_t modelVertexCount = 0;

    /// Returns true when the mesh contains no geometry at all.
    [[nodiscard]] bool isEmpty() const { return quadCount == 0 && modelVertexCount == 0; }
};

} // namespace voxel::renderer
