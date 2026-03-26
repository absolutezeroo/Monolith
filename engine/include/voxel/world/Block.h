#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

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
};

} // namespace voxel::world
