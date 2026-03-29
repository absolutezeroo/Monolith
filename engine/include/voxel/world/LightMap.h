#pragma once

#include "voxel/core/Assert.h"

#include <cstdint>
#include <cstring>

namespace voxel::world
{

/// Per-section light storage: 4096 bytes for a 16x16x16 section.
/// Each byte packs two 4-bit channels: [sky:4 | block:4] (sky in high nibble).
/// Indexing: Y-major layout matching ChunkSection: index = y*256 + z*16 + x.
class LightMap
{
  public:
    static constexpr int SIZE = 16;
    static constexpr int VOLUME = SIZE * SIZE * SIZE; // 4096

    LightMap() { clear(); }

    /// Zero all light values.
    void clear() { std::memset(m_data, 0, VOLUME); }

    /// Returns true if all light values are zero.
    [[nodiscard]] bool isClear() const
    {
        // Check 8 bytes at a time for speed.
        const auto* qwords = reinterpret_cast<const uint64_t*>(m_data);
        for (int i = 0; i < VOLUME / 8; ++i)
        {
            if (qwords[i] != 0)
            {
                return false;
            }
        }
        return true;
    }

    /// Get sky light (0-15) at local position.
    [[nodiscard]] uint8_t getSkyLight(int x, int y, int z) const
    {
        VX_ASSERT(
            x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE,
            "LightMap::getSkyLight out of bounds");
        return static_cast<uint8_t>((m_data[toIndex(x, y, z)] >> 4) & 0xF);
    }

    /// Get block light (0-15) at local position.
    [[nodiscard]] uint8_t getBlockLight(int x, int y, int z) const
    {
        VX_ASSERT(
            x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE,
            "LightMap::getBlockLight out of bounds");
        return static_cast<uint8_t>(m_data[toIndex(x, y, z)] & 0xF);
    }

    /// Set sky light (0-15) at local position, preserving block light.
    void setSkyLight(int x, int y, int z, uint8_t val)
    {
        VX_ASSERT(
            x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE,
            "LightMap::setSkyLight out of bounds");
        VX_ASSERT(val <= 15, "Sky light value must be 0-15");
        int idx = toIndex(x, y, z);
        m_data[idx] = static_cast<uint8_t>((val << 4) | (m_data[idx] & 0x0F));
    }

    /// Set block light (0-15) at local position, preserving sky light.
    void setBlockLight(int x, int y, int z, uint8_t val)
    {
        VX_ASSERT(
            x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE,
            "LightMap::setBlockLight out of bounds");
        VX_ASSERT(val <= 15, "Block light value must be 0-15");
        int idx = toIndex(x, y, z);
        m_data[idx] = static_cast<uint8_t>((m_data[idx] & 0xF0) | (val & 0x0F));
    }

    /// Get raw packed byte [sky:4 | block:4] at local position.
    [[nodiscard]] uint8_t getRaw(int x, int y, int z) const
    {
        VX_ASSERT(
            x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE,
            "LightMap::getRaw out of bounds");
        return m_data[toIndex(x, y, z)];
    }

    /// Set raw packed byte [sky:4 | block:4] at local position.
    void setRaw(int x, int y, int z, uint8_t val)
    {
        VX_ASSERT(
            x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE,
            "LightMap::setRaw out of bounds");
        m_data[toIndex(x, y, z)] = val;
    }

    /// Read-only pointer to the raw light data array for bulk access.
    [[nodiscard]] const uint8_t* data() const { return m_data; }

    /// Writable pointer to the raw light data array for bulk copy.
    [[nodiscard]] uint8_t* data() { return m_data; }

  private:
    uint8_t m_data[VOLUME];

    [[nodiscard]] static constexpr int toIndex(int x, int y, int z) { return y * 256 + z * 16 + x; }
};

} // namespace voxel::world
