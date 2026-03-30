#pragma once

// Internal header — shared by ChunkSerializer, BlockMetadata, BlockInventory.
// Lives in src/ (not include/) because it is not part of the public API.

#include "voxel/core/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace voxel::world
{

class BinaryWriter
{
public:
    void writeU8(uint8_t v) { m_buf.push_back(v); }

    void writeU16(uint16_t v)
    {
        m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    void writeU32(uint32_t v)
    {
        m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    void writeU64(uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
        {
            m_buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    }

    void writeString(const std::string& s)
    {
        writeU16(static_cast<uint16_t>(s.size()));
        m_buf.insert(m_buf.end(), s.begin(), s.end());
    }

    void writeFloat(float v)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(float));
        writeU32(bits);
    }

    void writeI32(int32_t v) { writeU32(static_cast<uint32_t>(v)); }

    [[nodiscard]] std::vector<uint8_t>& data() { return m_buf; }
    [[nodiscard]] const std::vector<uint8_t>& data() const { return m_buf; }

private:
    std::vector<uint8_t> m_buf;
};

class BinaryReader
{
public:
    explicit BinaryReader(std::span<const uint8_t> data) : m_data(data) {}

    [[nodiscard]] bool hasRemaining(size_t bytes) const { return m_pos + bytes <= m_data.size(); }

    [[nodiscard]] core::Result<uint8_t> readU8()
    {
        if (!hasRemaining(1))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint8"});
        }
        return m_data[m_pos++];
    }

    [[nodiscard]] core::Result<uint16_t> readU16()
    {
        if (!hasRemaining(2))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint16"});
        }
        uint16_t v = static_cast<uint16_t>(m_data[m_pos]) | (static_cast<uint16_t>(m_data[m_pos + 1]) << 8);
        m_pos += 2;
        return v;
    }

    [[nodiscard]] core::Result<uint32_t> readU32()
    {
        if (!hasRemaining(4))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint32"});
        }
        uint32_t v = static_cast<uint32_t>(m_data[m_pos]) | (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
                     (static_cast<uint32_t>(m_data[m_pos + 2]) << 16) |
                     (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return v;
    }

    [[nodiscard]] core::Result<uint64_t> readU64()
    {
        if (!hasRemaining(8))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint64"});
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
        {
            v |= static_cast<uint64_t>(m_data[m_pos + i]) << (i * 8);
        }
        m_pos += 8;
        return v;
    }

    [[nodiscard]] core::Result<std::string> readString()
    {
        auto lenResult = readU16();
        if (!lenResult.has_value())
        {
            return std::unexpected(lenResult.error());
        }
        uint16_t len = lenResult.value();
        if (!hasRemaining(len))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected string"});
        }
        std::string s(reinterpret_cast<const char*>(m_data.data() + m_pos), len);
        m_pos += len;
        return s;
    }

    [[nodiscard]] core::Result<int32_t> readI32()
    {
        auto r = readU32();
        if (!r.has_value())
        {
            return std::unexpected(r.error());
        }
        return static_cast<int32_t>(r.value());
    }

    [[nodiscard]] core::Result<float> readFloat()
    {
        auto r = readU32();
        if (!r.has_value())
        {
            return std::unexpected(r.error());
        }
        float f = 0;
        uint32_t bits = r.value();
        std::memcpy(&f, &bits, sizeof(float));
        return f;
    }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
};

} // namespace voxel::world
