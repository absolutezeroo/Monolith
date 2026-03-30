#pragma once

#include "voxel/core/Result.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace voxel::world
{

class BinaryWriter;
class BinaryReader;

/// Per-block key-value store with typed accessors.
/// Stores string, int32, or float values. Used by mods for signs, furnaces, etc.
class BlockMetadata
{
public:
    using Value = std::variant<std::string, int32_t, float>;

    void setString(const std::string& key, const std::string& value);
    [[nodiscard]] std::string getString(const std::string& key, const std::string& defaultVal = "") const;

    void setInt(const std::string& key, int32_t value);
    [[nodiscard]] int32_t getInt(const std::string& key, int32_t defaultVal = 0) const;

    void setFloat(const std::string& key, float value);
    [[nodiscard]] float getFloat(const std::string& key, float defaultVal = 0.0f) const;

    [[nodiscard]] bool contains(const std::string& key) const;
    void erase(const std::string& key);
    void clear();
    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t size() const;

    // Serialization
    void serialize(BinaryWriter& writer) const;
    [[nodiscard]] static core::Result<BlockMetadata> deserialize(BinaryReader& reader);

private:
    std::unordered_map<std::string, Value> m_data;
};

} // namespace voxel::world
