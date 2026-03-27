#include "voxel/core/ConcurrentQueue.h"
#include "voxel/core/JobSystem.h"
#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/renderer/MeshJobTypes.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace voxel::core;
using namespace voxel::renderer;
using namespace voxel::world;

// Helper: register a basic opaque block.
static uint16_t registerStone(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:stone";
    def.isSolid = true;
    def.isTransparent = false;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return registry.getIdByName("base:stone");
}

static constexpr std::array<const ChunkSection*, 6> NO_NEIGHBORS = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// ── Snapshot tests ──────────────────────────────────────────────────────────────

TEST_CASE("MeshJobInput snapshot correctly copies section data", "[renderer][meshing][async]")
{
    ChunkSection original;
    original.setBlock(0, 0, 0, 42);
    original.setBlock(5, 10, 3, 99);
    original.setBlock(15, 15, 15, 1234);

    MeshJobInput input;
    input.section = original;
    input.chunkCoord = {7, 3};
    input.sectionY = 5;

    // Verify copy is independent
    REQUIRE(input.section.getBlock(0, 0, 0) == 42);
    REQUIRE(input.section.getBlock(5, 10, 3) == 99);
    REQUIRE(input.section.getBlock(15, 15, 15) == 1234);
    REQUIRE(input.chunkCoord == glm::ivec2(7, 3));
    REQUIRE(input.sectionY == 5);

    // Modify original — snapshot should be unaffected
    original.setBlock(0, 0, 0, 999);
    REQUIRE(input.section.getBlock(0, 0, 0) == 42);
}

TEST_CASE("MeshJobInput correctly flags absent neighbors as nullptr", "[renderer][meshing][async]")
{
    MeshJobInput input;

    // Set some neighbors present, some absent
    ChunkSection neighborSection;
    neighborSection.setBlock(0, 0, 0, 10);

    input.neighbors[0] = neighborSection;
    input.hasNeighbor[0] = true;

    input.hasNeighbor[1] = false;
    input.hasNeighbor[2] = false;

    input.neighbors[3] = neighborSection;
    input.hasNeighbor[3] = true;

    input.hasNeighbor[4] = false;
    input.hasNeighbor[5] = false;

    // Reconstruct neighbor pointers (same logic as MeshChunkTask::ExecuteRange)
    std::array<const ChunkSection*, 6> neighbors{};
    for (int i = 0; i < 6; ++i)
    {
        neighbors[i] = input.hasNeighbor[i] ? &input.neighbors[i] : nullptr;
    }

    REQUIRE(neighbors[0] != nullptr);
    REQUIRE(neighbors[1] == nullptr);
    REQUIRE(neighbors[2] == nullptr);
    REQUIRE(neighbors[3] != nullptr);
    REQUIRE(neighbors[4] == nullptr);
    REQUIRE(neighbors[5] == nullptr);

    // Verify the present neighbor has correct data
    REQUIRE(neighbors[0]->getBlock(0, 0, 0) == 10);
}

// ── MeshChunkTask determinism test ──────────────────────────────────────────────

TEST_CASE("MeshChunkTask produces same mesh as synchronous buildGreedy", "[renderer][meshing][async]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    // Create a section with some blocks
    ChunkSection section;
    section.setBlock(8, 8, 8, stoneId);
    section.setBlock(8, 9, 8, stoneId);
    section.setBlock(9, 8, 8, stoneId);

    // Synchronous reference mesh
    ChunkMesh syncMesh = builder.buildGreedy(section, NO_NEIGHBORS);
    REQUIRE(syncMesh.quadCount > 0);

    // Async: create snapshot, run task, get result
    MeshJobInput input;
    input.section = section;
    input.chunkCoord = {0, 0};
    input.sectionY = 0;
    for (int i = 0; i < 6; ++i)
    {
        input.hasNeighbor[i] = false;
    }

    ConcurrentQueue<MeshResult> resultQueue;
    MeshChunkTask task(std::move(input), builder, resultQueue);

    // Execute synchronously (simulates what a worker thread would do)
    task.ExecuteRange({0, 1}, 0);

    auto result = resultQueue.tryPop();
    REQUIRE(result.has_value());

    // Verify identical output
    REQUIRE(result->mesh.quadCount == syncMesh.quadCount);
    REQUIRE(result->mesh.quads.size() == syncMesh.quads.size());
    for (size_t i = 0; i < syncMesh.quads.size(); ++i)
    {
        REQUIRE(result->mesh.quads[i] == syncMesh.quads[i]);
    }
    REQUIRE(result->mesh.modelVertexCount == syncMesh.modelVertexCount);
    REQUIRE(result->chunkCoord == glm::ivec2(0, 0));
    REQUIRE(result->sectionY == 0);
}

// ── End-to-end async test ───────────────────────────────────────────────────────

TEST_CASE("End-to-end: snapshot → dispatch via JobSystem → poll result", "[renderer][meshing][async]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    JobSystem js;
    auto initResult = js.init();
    REQUIRE(initResult.has_value());

    // Create a section with a single block
    ChunkSection section;
    section.setBlock(4, 4, 4, stoneId);

    MeshJobInput input;
    input.section = section;
    input.chunkCoord = {2, 3};
    input.sectionY = 1;
    for (int i = 0; i < 6; ++i)
    {
        input.hasNeighbor[i] = false;
    }

    ConcurrentQueue<MeshResult> resultQueue;

    // Dispatch task via job system
    auto task = std::make_unique<MeshChunkTask>(std::move(input), builder, resultQueue);
    js.getScheduler().AddTaskSetToPipe(task.get());
    js.getScheduler().WaitforTask(task.get());

    // Poll result
    auto result = resultQueue.tryPop();
    REQUIRE(result.has_value());
    REQUIRE(result->chunkCoord == glm::ivec2(2, 3));
    REQUIRE(result->sectionY == 1);
    REQUIRE(result->mesh.quadCount > 0); // Single block = 6 faces

    // Compare with sync reference
    ChunkSection refSection;
    refSection.setBlock(4, 4, 4, stoneId);
    ChunkMesh refMesh = builder.buildGreedy(refSection, NO_NEIGHBORS);
    REQUIRE(result->mesh.quadCount == refMesh.quadCount);

    js.shutdown();
}

// ── Cancellation test (AC10) ────────────────────────────────────────────────────

TEST_CASE("Cancellation: unloaded chunk results are discarded (AC10)", "[renderer][meshing][async]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    JobSystem js;
    auto initResult = js.init();
    REQUIRE(initResult.has_value());

    ChunkManager mgr;
    mgr.setJobSystem(&js);
    mgr.setMeshBuilder(&builder);

    // Load chunk at (5,5) and place a block to make section 0 dirty
    mgr.loadChunk({5, 5});
    mgr.setBlock({5 * 16 + 8, 8, 5 * 16 + 8}, stoneId);

    // Dispatch mesh task via update
    mgr.update(glm::dvec3(88.0, 8.0, 88.0));

    // Wait for in-flight tasks to complete — result is now in the queue
    mgr.shutdown();

    // Unload the chunk BEFORE polling results
    mgr.unloadChunk({5, 5});

    // Poll results — should discard because chunk is no longer loaded
    mgr.update(glm::dvec3(0.0, 0.0, 0.0));

    // Verify no mesh stored for the unloaded chunk
    REQUIRE(mgr.getMesh({5, 5}, 0) == nullptr);

    js.shutdown();
}
