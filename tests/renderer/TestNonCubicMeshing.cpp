#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/renderer/ModelMesher.h"
#include "voxel/renderer/ModelRegistry.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using namespace voxel::renderer;
using namespace voxel::world;

// --- Test helpers ---

static constexpr std::array<const ChunkSection*, 6> NO_NEIGHBORS = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// Helpers return base state ID (the value to use in setBlock / getBlockType).
// Do NOT use getIdByName — it returns the type index, which diverges from base state ID
// when multi-state blocks consume multiple state ID slots.

static uint16_t registerStone(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:stone";
    def.isSolid = true;
    def.isTransparent = false;
    def.modelType = ModelType::FullCube;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return result.value();
}

static uint16_t registerSlab(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:stone_slab";
    def.isSolid = true;
    def.isTransparent = false;
    def.modelType = ModelType::Slab;
    def.properties = {{.name = "half", .values = {"bottom", "top"}}};
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return result.value();
}

static uint16_t registerCross(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:flower";
    def.isSolid = false;
    def.isTransparent = true;
    def.modelType = ModelType::Cross;
    def.renderType = RenderType::Cutout;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return result.value();
}

static uint16_t registerTorch(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:torch";
    def.isSolid = false;
    def.isTransparent = true;
    def.modelType = ModelType::Torch;
    def.renderType = RenderType::Cutout;
    def.properties = {{.name = "facing", .values = {"up", "north", "south", "east", "west"}}};
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return result.value();
}

static uint32_t countFace(const ChunkMesh& mesh, BlockFace face)
{
    uint32_t count = 0;
    for (const uint64_t quad : mesh.quads)
    {
        if (unpackFace(quad) == face)
        {
            ++count;
        }
    }
    return count;
}

// --- Tests ---

TEST_CASE("Non-cubic meshing", "[renderer][meshing][non-cubic]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("empty section produces 0 quads and 0 model vertices")
    {
        ChunkSection section;
        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 0);
        REQUIRE(mesh.modelVertexCount == 0);
        REQUIRE(mesh.isEmpty());
    }

    SECTION("single slab block produces model vertices and no quads")
    {
        uint16_t slabId = registerSlab(registry);
        ChunkSection section;
        section.setBlock(8, 8, 8, slabId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // Slab should produce model vertices, not quads.
        REQUIRE(mesh.quadCount == 0);
        // Slab is a box with 6 faces × 6 vertices = 36 model vertices.
        REQUIRE(mesh.modelVertexCount == 36);
        REQUIRE_FALSE(mesh.isEmpty());
    }

    SECTION("slab on stone — face culling between cubic and non-cubic blocks")
    {
        uint16_t slabId = registerSlab(registry);
        ChunkSection section;
        // Stone at (8, 7, 8), bottom slab at (8, 8, 8)
        section.setBlock(8, 7, 8, stoneId);
        section.setBlock(8, 8, 8, slabId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // Default slab state is "half=bottom" → slab from y=0 to y=0.5 in local space.
        // Stone's PosY: neighbor is slab, opposite face = NegY.
        // slab.isFullFace(NegY=3, {half=bottom}) = true → stone's PosY face is culled.
        REQUIRE(countFace(mesh, BlockFace::PosY) == 0); // culled by slab's full bottom

        // Slab's NegY: neighbor is stone (FullCube, opaque) → culled.
        // Slab emits 5 visible faces × 6 vertices = 30 model vertices.
        REQUIRE(mesh.modelVertexCount == 30);
    }

    SECTION("cross block (flower) produces 24 model vertices (4 quads × 6 verts)")
    {
        uint16_t flowerId = registerCross(registry);
        ChunkSection section;
        section.setBlock(5, 5, 5, flowerId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // Cross: 2 diagonal quads × 2 sides (front+back) = 4 quads × 6 vertices = 24
        REQUIRE(mesh.quadCount == 0);
        REQUIRE(mesh.modelVertexCount == 24);
    }

    SECTION("torch block produces model vertices")
    {
        uint16_t torchId = registerTorch(registry);
        ChunkSection section;
        section.setBlock(3, 3, 3, torchId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // Torch is a thin box: 6 faces × 6 vertices = 36
        REQUIRE(mesh.quadCount == 0);
        REQUIRE(mesh.modelVertexCount == 36);
    }

    SECTION("cubic block next to cross emits face toward cross")
    {
        uint16_t flowerId = registerCross(registry);
        ChunkSection section;
        section.setBlock(8, 8, 8, stoneId);
        section.setBlock(9, 8, 8, flowerId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // Stone block PosX face: neighbor is flower (transparent + non-cubic → isFullFace=false).
        // Face should be emitted because flower is transparent.
        REQUIRE(countFace(mesh, BlockFace::PosX) == 1);
        // Stone has all 6 faces (flower is transparent, so all faces of stone are emitted).
        REQUIRE(mesh.quadCount == 6);
    }

    SECTION("mixed section: cubic + non-cubic blocks together")
    {
        uint16_t slabId = registerSlab(registry);
        uint16_t flowerId = registerCross(registry);
        ChunkSection section;

        section.setBlock(0, 0, 0, stoneId);
        section.setBlock(1, 0, 0, slabId);
        section.setBlock(2, 0, 0, flowerId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // Stone: 6 faces. PosX neighbor is slab. Slab NegX is NOT full → stone PosX IS emitted.
        REQUIRE(mesh.quadCount == 6); // only stone generates quads
        // Slab: NegX neighbor is stone (FullCube, opaque) → NegX culled → 5 faces × 6 = 30 verts.
        // Flower: cross geometry = 24 verts (no face culling for diagonal quads).
        // Total: 30 + 24 = 54 model vertices.
        REQUIRE(mesh.modelVertexCount == 54);
        REQUIRE_FALSE(mesh.isEmpty());
    }
}

TEST_CASE("ModelVertex construction and fields", "[renderer][meshing][non-cubic]")
{
    ModelVertex v{};
    v.position = {1.0f, 2.0f, 3.0f};
    v.normal = {0.0f, 1.0f, 0.0f};
    v.uv = {0.5f, 0.75f};
    v.blockStateId = 42;
    v.ao = 2;
    v.flags = 0x05; // tint=1, waving=2

    REQUIRE(v.position.x == 1.0f);
    REQUIRE(v.position.y == 2.0f);
    REQUIRE(v.position.z == 3.0f);
    REQUIRE(v.normal.y == 1.0f);
    REQUIRE(v.uv.x == 0.5f);
    REQUIRE(v.uv.y == 0.75f);
    REQUIRE(v.blockStateId == 42);
    REQUIRE(v.ao == 2);
    REQUIRE(v.flags == 0x05);
}

TEST_CASE("isFullFace truth table", "[world][block]")
{
    SECTION("FullCube — always true for all faces")
    {
        BlockDefinition def;
        def.modelType = ModelType::FullCube;
        StateMap state;

        for (uint8_t f = 0; f < 6; ++f)
        {
            REQUIRE(def.isFullFace(f, state));
        }
    }

    SECTION("Slab bottom half — NegY true, PosY false, sides false")
    {
        BlockDefinition def;
        def.modelType = ModelType::Slab;
        StateMap state = {{"half", "bottom"}};

        REQUIRE(def.isFullFace(3, state));       // NegY → true
        REQUIRE_FALSE(def.isFullFace(2, state));  // PosY → false
        REQUIRE_FALSE(def.isFullFace(0, state));  // PosX → false
        REQUIRE_FALSE(def.isFullFace(1, state));  // NegX → false
        REQUIRE_FALSE(def.isFullFace(4, state));  // PosZ → false
        REQUIRE_FALSE(def.isFullFace(5, state));  // NegZ → false
    }

    SECTION("Slab top half — PosY true, NegY false, sides false")
    {
        BlockDefinition def;
        def.modelType = ModelType::Slab;
        StateMap state = {{"half", "top"}};

        REQUIRE(def.isFullFace(2, state));       // PosY → true
        REQUIRE_FALSE(def.isFullFace(3, state));  // NegY → false
        REQUIRE_FALSE(def.isFullFace(0, state));  // PosX → false
        REQUIRE_FALSE(def.isFullFace(1, state));  // NegX → false
    }

    SECTION("Slab default state (no half property) — NegY true (defaults to bottom)")
    {
        BlockDefinition def;
        def.modelType = ModelType::Slab;
        StateMap state; // empty — defaults to bottom

        REQUIRE(def.isFullFace(3, state));       // NegY → true (bottom)
        REQUIRE_FALSE(def.isFullFace(2, state));  // PosY → false
    }

    SECTION("Cross — always false")
    {
        BlockDefinition def;
        def.modelType = ModelType::Cross;
        StateMap state;

        for (uint8_t f = 0; f < 6; ++f)
        {
            REQUIRE_FALSE(def.isFullFace(f, state));
        }
    }

    SECTION("Torch — always false")
    {
        BlockDefinition def;
        def.modelType = ModelType::Torch;
        StateMap state;

        for (uint8_t f = 0; f < 6; ++f)
        {
            REQUIRE_FALSE(def.isFullFace(f, state));
        }
    }

    SECTION("Stair — always false (stub)")
    {
        BlockDefinition def;
        def.modelType = ModelType::Stair;
        StateMap state;

        for (uint8_t f = 0; f < 6; ++f)
        {
            REQUIRE_FALSE(def.isFullFace(f, state));
        }
    }

    SECTION("Connected — always false (stub)")
    {
        BlockDefinition def;
        def.modelType = ModelType::Connected;
        StateMap state;

        for (uint8_t f = 0; f < 6; ++f)
        {
            REQUIRE_FALSE(def.isFullFace(f, state));
        }
    }
}

TEST_CASE("ChunkMesh::isEmpty", "[renderer][meshing]")
{
    SECTION("default constructed mesh is empty")
    {
        ChunkMesh mesh;
        REQUIRE(mesh.isEmpty());
    }

    SECTION("mesh with quads is not empty")
    {
        ChunkMesh mesh;
        mesh.quadCount = 1;
        REQUIRE_FALSE(mesh.isEmpty());
    }

    SECTION("mesh with model vertices is not empty")
    {
        ChunkMesh mesh;
        mesh.modelVertexCount = 1;
        REQUIRE_FALSE(mesh.isEmpty());
    }
}

TEST_CASE("Greedy mesher skips non-cubic blocks", "[renderer][meshing][non-cubic]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    uint16_t flowerId = registerCross(registry);
    MeshBuilder builder(registry);

    SECTION("empty section — greedy also produces no output")
    {
        ChunkSection section;
        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);
        REQUIRE(mesh.quadCount == 0);
        REQUIRE(mesh.modelVertexCount == 0);
        REQUIRE(mesh.isEmpty());
    }

    SECTION("section with only non-cubic blocks — no quads, only model vertices")
    {
        ChunkSection section;
        section.setBlock(4, 4, 4, flowerId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);
        REQUIRE(mesh.quadCount == 0);
        REQUIRE(mesh.modelVertexCount == 24); // cross = 24 vertices
    }

    SECTION("mixed cubic and non-cubic — greedy merges only cubic, emits model for non-cubic")
    {
        ChunkSection section;
        // Row of 4 stone blocks that should be greedy-merged.
        section.setBlock(0, 0, 0, stoneId);
        section.setBlock(1, 0, 0, stoneId);
        section.setBlock(2, 0, 0, stoneId);
        section.setBlock(3, 0, 0, stoneId);
        // One flower in between — should NOT interfere with greedy merging.
        section.setBlock(5, 0, 0, flowerId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Stone blocks should be greedy-merged into larger quads.
        // Flower should produce model vertices.
        REQUIRE(mesh.quadCount > 0);
        REQUIRE(mesh.modelVertexCount == 24); // flower
    }
}
