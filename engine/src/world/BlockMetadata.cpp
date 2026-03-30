#include "voxel/world/BlockMetadata.h"

#include "BinaryIO.h"

namespace voxel::world
{

void BlockMetadata::setString(const std::string& key, const std::string& value)
{
    m_data[key] = value;
}

std::string BlockMetadata::getString(const std::string& key, const std::string& defaultVal) const
{
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        return defaultVal;
    }
    if (auto* s = std::get_if<std::string>(&it->second))
    {
        return *s;
    }
    return defaultVal;
}

void BlockMetadata::setInt(const std::string& key, int32_t value)
{
    m_data[key] = value;
}

int32_t BlockMetadata::getInt(const std::string& key, int32_t defaultVal) const
{
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        return defaultVal;
    }
    if (auto* i = std::get_if<int32_t>(&it->second))
    {
        return *i;
    }
    return defaultVal;
}

void BlockMetadata::setFloat(const std::string& key, float value)
{
    m_data[key] = value;
}

float BlockMetadata::getFloat(const std::string& key, float defaultVal) const
{
    auto it = m_data.find(key);
    if (it == m_data.end())
    {
        return defaultVal;
    }
    if (auto* f = std::get_if<float>(&it->second))
    {
        return *f;
    }
    return defaultVal;
}

bool BlockMetadata::contains(const std::string& key) const
{
    return m_data.contains(key);
}

void BlockMetadata::erase(const std::string& key)
{
    m_data.erase(key);
}

void BlockMetadata::clear()
{
    m_data.clear();
}

bool BlockMetadata::empty() const
{
    return m_data.empty();
}

size_t BlockMetadata::size() const
{
    return m_data.size();
}

// Serialization format:
// [u16 count] [for each: u16 key_len, key_bytes, u8 type_tag, value_data]
// Type tags: 0 = string (u16 len + bytes), 1 = int32 (4 bytes LE), 2 = float (4 bytes LE)

void BlockMetadata::serialize(BinaryWriter& writer) const
{
    writer.writeU16(static_cast<uint16_t>(m_data.size()));
    for (const auto& [key, value] : m_data)
    {
        writer.writeString(key);
        if (auto* s = std::get_if<std::string>(&value))
        {
            writer.writeU8(0);
            writer.writeString(*s);
        }
        else if (auto* i = std::get_if<int32_t>(&value))
        {
            writer.writeU8(1);
            writer.writeI32(*i);
        }
        else if (auto* f = std::get_if<float>(&value))
        {
            writer.writeU8(2);
            writer.writeFloat(*f);
        }
    }
}

core::Result<BlockMetadata> BlockMetadata::deserialize(BinaryReader& reader)
{
    auto countResult = reader.readU16();
    if (!countResult.has_value())
    {
        return std::unexpected(countResult.error());
    }
    uint16_t count = countResult.value();

    BlockMetadata meta;
    for (uint16_t i = 0; i < count; ++i)
    {
        auto keyResult = reader.readString();
        if (!keyResult.has_value())
        {
            return std::unexpected(keyResult.error());
        }

        auto tagResult = reader.readU8();
        if (!tagResult.has_value())
        {
            return std::unexpected(tagResult.error());
        }
        uint8_t tag = tagResult.value();

        switch (tag)
        {
        case 0: // string
        {
            auto valResult = reader.readString();
            if (!valResult.has_value())
            {
                return std::unexpected(valResult.error());
            }
            meta.m_data[keyResult.value()] = std::move(valResult.value());
            break;
        }
        case 1: // int32
        {
            auto valResult = reader.readI32();
            if (!valResult.has_value())
            {
                return std::unexpected(valResult.error());
            }
            meta.m_data[keyResult.value()] = valResult.value();
            break;
        }
        case 2: // float
        {
            auto valResult = reader.readFloat();
            if (!valResult.has_value())
            {
                return std::unexpected(valResult.error());
            }
            meta.m_data[keyResult.value()] = valResult.value();
            break;
        }
        default:
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Unknown metadata type tag: " + std::to_string(tag)});
        }
    }

    return meta;
}

} // namespace voxel::world
