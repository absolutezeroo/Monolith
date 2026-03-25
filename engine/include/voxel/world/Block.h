#pragma once

#include <cstdint>
#include <string>

namespace voxel::world
{

constexpr uint16_t BLOCK_AIR = 0;

struct BlockDefinition
{
    std::string stringId;
    uint16_t numericId = 0;
    bool isSolid = true;
    bool isTransparent = false;
    bool hasCollision = true;
    uint8_t lightEmission = 0;
    uint8_t lightFilter = 15;
    float hardness = 1.0f;
    uint16_t textureIndices[6] = {};
    std::string dropItem;
};

} // namespace voxel::world
