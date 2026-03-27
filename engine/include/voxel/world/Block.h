#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace voxel::world
{

constexpr uint16_t BLOCK_AIR = 0;

enum class RenderType : uint8_t
{
    Opaque,
    Cutout,
    Translucent
};

enum class ModelType : uint8_t
{
    FullCube,
    Slab,
    Stair,
    Cross,
    Torch,
    Connected,
    JsonModel,
    MeshModel,
    Custom
};

enum class LiquidType : uint8_t
{
    None,
    Source,
    Flowing
};

enum class PushReaction : uint8_t
{
    Normal,
    Destroy,
    Block
};

/// A single block state property (e.g., facing with values [north, south, east, west]).
struct BlockStateProperty
{
    std::string name;
    std::vector<std::string> values;
};

/// Maps property names to their current string values.
using StateMap = std::unordered_map<std::string, std::string>;

struct BlockDefinition
{
    // --- Identity ---
    std::string stringId;
    uint16_t numericId = 0;

    // --- Core properties ---
    bool isSolid = true;
    bool isTransparent = false;
    bool hasCollision = true;
    float hardness = 1.0f;
    uint8_t lightEmission = 0;
    uint8_t lightFilter = 0;

    // --- Rendering ---
    RenderType renderType = RenderType::Opaque;
    ModelType modelType = ModelType::FullCube;
    uint16_t textureIndices[6] = {};
    uint8_t tintIndex = 0;
    uint8_t waving = 0;

    // --- Physics / interaction ---
    bool isClimbable = false;
    uint8_t moveResistance = 0;
    uint32_t damagePerSecond = 0;
    uint8_t drowning = 0;
    bool isBuildableTo = false;
    bool isFloodable = false;
    bool isReplaceable = false;

    // --- Tool / mining groups ---
    std::unordered_map<std::string, int> groups;

    // --- Drop ---
    std::string dropItem;

    // --- Sound stubs ---
    std::string soundFootstep;
    std::string soundDig;
    std::string soundPlace;

    // --- Liquid stubs ---
    LiquidType liquidType = LiquidType::None;
    uint8_t liquidViscosity = 0;
    uint8_t liquidRange = 8;
    bool liquidRenewable = true;
    std::string liquidAlternativeFlowing;
    std::string liquidAlternativeSource;

    // --- Visual effects ---
    uint32_t postEffectColor = 0;

    // --- Mechanical behavior ---
    PushReaction pushReaction = PushReaction::Normal;
    bool isFallingBlock = false;

    // --- Redstone/signal stubs ---
    uint8_t powerOutput = 0;
    bool isPowerSource = false;
    bool isPowerConductor = true;

    // --- Block states ---
    std::vector<BlockStateProperty> properties;
    uint16_t baseStateId = 0;
    uint16_t stateCount = 1;

    /// Returns whether the given face fully covers the 1x1 block boundary.
    /// Used for face culling between cubic and non-cubic blocks.
    /// @param faceIndex Face direction index (matches BlockFace enum: PosX=0..NegZ=5).
    /// @param state Current block state properties (needed for state-dependent models like Slab).
    [[nodiscard]] bool isFullFace(uint8_t faceIndex, const StateMap& state = {}) const
    {
        switch (modelType)
        {
        case ModelType::FullCube:
            return true;
        case ModelType::Slab:
        {
            // PosY=2, NegY=3
            auto it = state.find("half");
            bool isTop = (it != state.end() && it->second == "top");
            if (faceIndex == 2) // PosY
                return isTop;
            if (faceIndex == 3) // NegY
                return !isTop;
            return false; // Side faces are half-height
        }
        default:
            return false;
        }
    }
};

} // namespace voxel::world
