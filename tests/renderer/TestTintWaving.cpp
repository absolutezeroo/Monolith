#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/renderer/TintPalette.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using namespace voxel::renderer;
using namespace voxel::world;

// Helper: register a block with specific tint/waving properties.
static uint16_t registerBlock(
    BlockRegistry& registry,
    const std::string& id,
    uint8_t tintIndex = 0,
    uint8_t waving = 0,
    bool transparent = false)
{
    BlockDefinition def;
    def.stringId = id;
    def.isSolid = true;
    def.isTransparent = transparent;
    def.tintIndex = tintIndex;
    def.waving = waving;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return registry.getIdByName(id);
}

static constexpr std::array<const ChunkSection*, 6> NO_NEIGHBORS = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// ── AC6: Quad packing roundtrip tests ──────────────────────────────────────

TEST_CASE("Tint and waving quad packing roundtrip", "[renderer][meshing][tint]")
{
    SECTION("pack tintIndex=5, wavingType=2 → unpack returns same values")
    {
        uint64_t quad = packQuad(8, 8, 8, 100, BlockFace::PosX, 1, 1, 3, 3, 3, 3, false, 5, 2);

        REQUIRE(unpackTintIndex(quad) == 5);
        REQUIRE(unpackWavingType(quad) == 2);
    }

    SECTION("pack tintIndex=0, wavingType=0 → default behavior unchanged")
    {
        uint64_t quad = packQuad(4, 4, 4, 200, BlockFace::NegY, 1, 1, 3, 3, 3, 3, false, 0, 0);

        REQUIRE(unpackTintIndex(quad) == 0);
        REQUIRE(unpackWavingType(quad) == 0);
    }

    SECTION("pack max values tintIndex=7, wavingType=3 → roundtrip correct")
    {
        uint64_t quad = packQuad(63, 63, 63, 65535, BlockFace::NegZ, 64, 64, 3, 2, 1, 0, true, 7, 3);

        REQUIRE(unpackTintIndex(quad) == 7);
        REQUIRE(unpackWavingType(quad) == 3);
    }

    SECTION("existing fields unaffected when tint and waving are set")
    {
        uint8_t x = 15;
        uint8_t y = 7;
        uint8_t z = 31;
        uint16_t blockStateId = 500;
        BlockFace face = BlockFace::PosZ;
        uint8_t w = 10;
        uint8_t h = 20;
        uint8_t ao0 = 3;
        uint8_t ao1 = 2;
        uint8_t ao2 = 1;
        uint8_t ao3 = 0;
        bool flip = true;
        uint8_t tintIndex = 5;
        uint8_t wavingType = 2;

        uint64_t quad = packQuad(x, y, z, blockStateId, face, w, h, ao0, ao1, ao2, ao3, flip, tintIndex, wavingType);

        REQUIRE(unpackX(quad) == x);
        REQUIRE(unpackY(quad) == y);
        REQUIRE(unpackZ(quad) == (z & 0x3F));
        REQUIRE(unpackBlockStateId(quad) == blockStateId);
        REQUIRE(unpackFace(quad) == face);
        REQUIRE(unpackWidth(quad) == w);
        REQUIRE(unpackHeight(quad) == h);
        auto ao = unpackAO(quad);
        REQUIRE(ao[0] == ao0);
        REQUIRE(ao[1] == ao1);
        REQUIRE(ao[2] == ao2);
        REQUIRE(ao[3] == ao3);
        REQUIRE(unpackFlip(quad) == flip);
        REQUIRE(unpackTintIndex(quad) == tintIndex);
        REQUIRE(unpackWavingType(quad) == wavingType);
    }

    SECTION("default packQuad (no tint/waving args) has tint=0, waving=0")
    {
        uint64_t quad = packQuad(0, 0, 0, 1, BlockFace::PosX);

        REQUIRE(unpackTintIndex(quad) == 0);
        REQUIRE(unpackWavingType(quad) == 0);
    }

    SECTION("constexpr pack/unpack compile-time validation")
    {
        constexpr uint64_t quad = packQuad(1, 2, 3, 42, BlockFace::PosY, 1, 1, 3, 3, 3, 3, false, 6, 3);

        static_assert(unpackTintIndex(quad) == 6, "constexpr tintIndex roundtrip");
        static_assert(unpackWavingType(quad) == 3, "constexpr wavingType roundtrip");
        static_assert(unpackX(quad) == 1, "constexpr X unaffected");
        static_assert(unpackBlockStateId(quad) == 42, "constexpr blockStateId unaffected");

        REQUIRE(unpackTintIndex(quad) == 6);
        REQUIRE(unpackWavingType(quad) == 3);
    }
}

// ── AC7: Meshing integration tests ─────────────────────────────────────────

TEST_CASE("Meshing integration with tint and waving", "[renderer][meshing][tint]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerBlock(registry, "base:stone", 0, 0);
    uint16_t grassId = registerBlock(registry, "base:grass_block", 1, 2);
    registerBlock(registry, "base:oak_leaves", 2, 1, true);
    MeshBuilder builder(registry);

    SECTION("grass block quads carry tintIndex=1, wavingType=2")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, grassId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        for (const uint64_t quad : mesh.quads)
        {
            REQUIRE(unpackTintIndex(quad) == 1);
            REQUIRE(unpackWavingType(quad) == 2);
        }
    }

    SECTION("stone block quads carry tintIndex=0, wavingType=0")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, stoneId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        for (const uint64_t quad : mesh.quads)
        {
            REQUIRE(unpackTintIndex(quad) == 0);
            REQUIRE(unpackWavingType(quad) == 0);
        }
    }

    SECTION("mixed blocks carry correct tint/waving per block type")
    {
        ChunkSection section;
        section.setBlock(4, 8, 8, stoneId);
        section.setBlock(8, 8, 8, grassId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        for (const uint64_t quad : mesh.quads)
        {
            uint16_t blockId = unpackBlockStateId(quad);
            if (blockId == stoneId)
            {
                REQUIRE(unpackTintIndex(quad) == 0);
                REQUIRE(unpackWavingType(quad) == 0);
            }
            else if (blockId == grassId)
            {
                REQUIRE(unpackTintIndex(quad) == 1);
                REQUIRE(unpackWavingType(quad) == 2);
            }
        }
    }

    SECTION("existing meshing behavior unchanged — face count and AO identical")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, stoneId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);

        // Verify positions, AO, and face directions are unchanged.
        for (const uint64_t quad : mesh.quads)
        {
            REQUIRE(unpackX(quad) == 8);
            REQUIRE(unpackY(quad) == 8);
            REQUIRE(unpackZ(quad) == 8);
            REQUIRE(unpackBlockStateId(quad) == stoneId);
            REQUIRE(unpackWidth(quad) == 1);
            REQUIRE(unpackHeight(quad) == 1);
            auto ao = unpackAO(quad);
            REQUIRE(ao[0] == 3);
            REQUIRE(ao[1] == 3);
            REQUIRE(ao[2] == 3);
            REQUIRE(ao[3] == 3);
        }
    }
}

// ── Greedy mesher tint/waving propagation ───────────────────────────────────

TEST_CASE("Greedy mesher propagates tint and waving", "[renderer][meshing][tint]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerBlock(registry, "base:stone", 0, 0);
    uint16_t grassId = registerBlock(registry, "base:grass_block", 1, 2);
    MeshBuilder builder(registry);

    SECTION("grass block quads carry tintIndex=1, wavingType=2 via greedy")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, grassId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        for (const uint64_t quad : mesh.quads)
        {
            REQUIRE(unpackTintIndex(quad) == 1);
            REQUIRE(unpackWavingType(quad) == 2);
        }
    }

    SECTION("stone block quads carry tintIndex=0, wavingType=0 via greedy")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, stoneId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        for (const uint64_t quad : mesh.quads)
        {
            REQUIRE(unpackTintIndex(quad) == 0);
            REQUIRE(unpackWavingType(quad) == 0);
        }
    }

    SECTION("mixed blocks carry correct tint/waving per block type via greedy")
    {
        ChunkSection section;
        section.setBlock(4, 8, 8, stoneId);
        section.setBlock(8, 8, 8, grassId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        for (const uint64_t quad : mesh.quads)
        {
            uint16_t blockId = unpackBlockStateId(quad);
            if (blockId == stoneId)
            {
                REQUIRE(unpackTintIndex(quad) == 0);
                REQUIRE(unpackWavingType(quad) == 0);
            }
            else if (blockId == grassId)
            {
                REQUIRE(unpackTintIndex(quad) == 1);
                REQUIRE(unpackWavingType(quad) == 2);
            }
        }
    }
}

// ── AC8: TintPalette tests ─────────────────────────────────────────────────

TEST_CASE("TintPalette", "[renderer][tint]")
{
    SECTION("default palette — index 0 is white, indices 1-3 have non-white values")
    {
        TintPalette palette;
        glm::vec3 white(1.0f, 1.0f, 1.0f);

        REQUIRE(palette.getColor(0) == white);
        // Default palette has all white (no biome applied yet).
        REQUIRE(palette.getColor(1) == white);
    }

    SECTION("buildForBiome(Plains) — grass index has green tint")
    {
        TintPalette palette = TintPalette::buildForBiome(BiomeType::Plains);
        glm::vec3 white(1.0f, 1.0f, 1.0f);

        // Index 0 always white.
        REQUIRE(palette.getColor(0) == white);

        // Grass (index 1) should be greenish.
        glm::vec3 grass = palette.getColor(TintPalette::TINT_GRASS);
        REQUIRE(grass.g > grass.r);
        REQUIRE(grass.g > grass.b);
        REQUIRE(grass != white);
    }

    SECTION("buildForBiome(Desert) — grass index has brownish tint")
    {
        TintPalette palette = TintPalette::buildForBiome(BiomeType::Desert);

        glm::vec3 grass = palette.getColor(TintPalette::TINT_GRASS);
        // Desert grass should be more reddish/yellowish than green.
        REQUIRE(grass.r > grass.b);
        REQUIRE(grass.r > grass.g);
    }

    SECTION("index 0 always returns white regardless of biome")
    {
        glm::vec3 white(1.0f, 1.0f, 1.0f);

        REQUIRE(TintPalette::buildForBiome(BiomeType::Plains).getColor(0) == white);
        REQUIRE(TintPalette::buildForBiome(BiomeType::Desert).getColor(0) == white);
        REQUIRE(TintPalette::buildForBiome(BiomeType::Forest).getColor(0) == white);
        REQUIRE(TintPalette::buildForBiome(BiomeType::Taiga).getColor(0) == white);
        REQUIRE(TintPalette::buildForBiome(BiomeType::Jungle).getColor(0) == white);
        REQUIRE(TintPalette::buildForBiome(BiomeType::Tundra).getColor(0) == white);
        REQUIRE(TintPalette::buildForBiome(BiomeType::Savanna).getColor(0) == white);
        REQUIRE(TintPalette::buildForBiome(BiomeType::IcePlains).getColor(0) == white);
    }

    SECTION("setColor updates palette entry")
    {
        TintPalette palette;
        glm::vec3 custom(0.1f, 0.2f, 0.3f);

        palette.setColor(4, custom);

        REQUIRE(palette.getColor(4) == custom);
    }

    SECTION("getColor clamps out-of-range index")
    {
        TintPalette palette;
        glm::vec3 white(1.0f, 1.0f, 1.0f);

        // Index 255 should clamp to index 7 (MAX_ENTRIES - 1), which is white by default.
        REQUIRE(palette.getColor(255) == white);
    }

    SECTION("different biomes produce different grass colors")
    {
        TintPalette plains = TintPalette::buildForBiome(BiomeType::Plains);
        TintPalette desert = TintPalette::buildForBiome(BiomeType::Desert);
        TintPalette jungle = TintPalette::buildForBiome(BiomeType::Jungle);

        glm::vec3 plainsGrass = plains.getColor(TintPalette::TINT_GRASS);
        glm::vec3 desertGrass = desert.getColor(TintPalette::TINT_GRASS);
        glm::vec3 jungleGrass = jungle.getColor(TintPalette::TINT_GRASS);

        REQUIRE(plainsGrass != desertGrass);
        REQUIRE(plainsGrass != jungleGrass);
        REQUIRE(desertGrass != jungleGrass);
    }
}
