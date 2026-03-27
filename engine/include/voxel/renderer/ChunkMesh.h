#pragma once

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
///   [30:39] Block state ID (0-1023)
///   [40:42] Face direction (0-5)
///   [43:44] AO corner 0 (0-3)
///   [45:46] AO corner 1 (0-3)
///   [47:48] AO corner 2 (0-3)
///   [49:50] AO corner 3 (0-3)
///   [51]    Quad diagonal flip
///   [52:63] Reserved for future stories (non-cubic, sky/block light, tint, waving)

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
    q |= static_cast<uint64_t>(blockStateId & 0x3FF) << 30;
    q |= static_cast<uint64_t>(static_cast<uint8_t>(face) & 0x7) << 40;
    q |= static_cast<uint64_t>(ao0 & 0x3) << 43;
    q |= static_cast<uint64_t>(ao1 & 0x3) << 45;
    q |= static_cast<uint64_t>(ao2 & 0x3) << 47;
    q |= static_cast<uint64_t>(ao3 & 0x3) << 49;
    q |= static_cast<uint64_t>(flip ? 1 : 0) << 51;
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
inline constexpr uint16_t unpackBlockStateId(uint64_t quad) { return static_cast<uint16_t>((quad >> 30) & 0x3FF); }

/// Unpack face direction.
inline constexpr BlockFace unpackFace(uint64_t quad)
{
    return static_cast<BlockFace>((quad >> 40) & 0x7);
}

/// Unpack all 4 AO corner values (0-3 each).
inline constexpr std::array<uint8_t, 4> unpackAO(uint64_t quad)
{
    return {
        static_cast<uint8_t>((quad >> 43) & 0x3),
        static_cast<uint8_t>((quad >> 45) & 0x3),
        static_cast<uint8_t>((quad >> 47) & 0x3),
        static_cast<uint8_t>((quad >> 49) & 0x3),
    };
}

/// Unpack quad diagonal flip flag.
inline constexpr bool unpackFlip(uint64_t quad) { return ((quad >> 51) & 0x1) != 0; }

/// Mesh data for a single chunk section.
struct ChunkMesh
{
    std::vector<uint64_t> quads;
    uint32_t quadCount = 0;
};

} // namespace voxel::renderer
