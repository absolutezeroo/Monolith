#include "voxel/world/ChunkManager.h"

#include "voxel/world/Block.h"

#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

using namespace voxel::world;

// ── Coordinate helper tests ─────────────────────────────────────────────────────

TEST_CASE("floorDiv — correct floor division for negative and positive values", "[world][chunkmanager]")
{
    // Positive dividends — same as truncation
    REQUIRE(floorDiv(0, 16) == 0);
    REQUIRE(floorDiv(1, 16) == 0);
    REQUIRE(floorDiv(15, 16) == 0);
    REQUIRE(floorDiv(16, 16) == 1);
    REQUIRE(floorDiv(31, 16) == 1);
    REQUIRE(floorDiv(32, 16) == 2);

    // Negative dividends — floor division differs from truncation
    REQUIRE(floorDiv(-1, 16) == -1);
    REQUIRE(floorDiv(-15, 16) == -1);
    REQUIRE(floorDiv(-16, 16) == -1);
    REQUIRE(floorDiv(-17, 16) == -2);
    REQUIRE(floorDiv(-32, 16) == -2);
    REQUIRE(floorDiv(-33, 16) == -3);
}

TEST_CASE("euclideanMod — always returns non-negative result", "[world][chunkmanager]")
{
    // Positive values — same as C++ %
    REQUIRE(euclideanMod(0, 16) == 0);
    REQUIRE(euclideanMod(1, 16) == 1);
    REQUIRE(euclideanMod(15, 16) == 15);
    REQUIRE(euclideanMod(16, 16) == 0);
    REQUIRE(euclideanMod(17, 16) == 1);

    // Negative values — C++ % gives negative, euclideanMod gives positive
    REQUIRE(euclideanMod(-1, 16) == 15);
    REQUIRE(euclideanMod(-15, 16) == 1);
    REQUIRE(euclideanMod(-16, 16) == 0);
    REQUIRE(euclideanMod(-17, 16) == 15);
    REQUIRE(euclideanMod(-32, 16) == 0);
}

TEST_CASE("worldToChunkCoord — world position to chunk column coordinate", "[world][chunkmanager]")
{
    // From story Dev Notes test table
    REQUIRE(worldToChunkCoord({0, 64, 0}) == glm::ivec2(0, 0));
    REQUIRE(worldToChunkCoord({15, 0, 15}) == glm::ivec2(0, 0));
    REQUIRE(worldToChunkCoord({16, 0, 0}) == glm::ivec2(1, 0));
    REQUIRE(worldToChunkCoord({-1, 0, 0}) == glm::ivec2(-1, 0));
    REQUIRE(worldToChunkCoord({-16, 0, 0}) == glm::ivec2(-1, 0));
    REQUIRE(worldToChunkCoord({-17, 0, -17}) == glm::ivec2(-2, -2));
    REQUIRE(worldToChunkCoord({31, 255, 31}) == glm::ivec2(1, 1));
}

TEST_CASE("worldToLocalPos — world position to chunk-local position", "[world][chunkmanager]")
{
    // From story Dev Notes test table
    REQUIRE(worldToLocalPos({0, 64, 0}) == glm::ivec3(0, 64, 0));
    REQUIRE(worldToLocalPos({15, 0, 15}) == glm::ivec3(15, 0, 15));
    REQUIRE(worldToLocalPos({16, 0, 0}) == glm::ivec3(0, 0, 0));
    REQUIRE(worldToLocalPos({-1, 0, 0}) == glm::ivec3(15, 0, 0));
    REQUIRE(worldToLocalPos({-16, 0, 0}) == glm::ivec3(0, 0, 0));
    REQUIRE(worldToLocalPos({-17, 0, -17}) == glm::ivec3(15, 0, 15));
    REQUIRE(worldToLocalPos({31, 255, 31}) == glm::ivec3(15, 255, 15));

    // Y is passed through directly
    REQUIRE(worldToLocalPos({5, 200, 3}).y == 200);
    REQUIRE(worldToLocalPos({5, 0, 3}).y == 0);
    REQUIRE(worldToLocalPos({5, 255, 3}).y == 255);
}

// ── ChunkCoordHash tests ────────────────────────────────────────────────────────

TEST_CASE("ChunkCoordHash — no collisions for common coordinate pairs", "[world][chunkmanager]")
{
    ChunkCoordHash hasher;
    std::unordered_set<size_t> hashes;

    // Hash a grid of chunk coords and verify no collisions
    int collisions = 0;
    constexpr int RANGE = 32;
    for (int x = -RANGE; x <= RANGE; ++x)
    {
        for (int z = -RANGE; z <= RANGE; ++z)
        {
            size_t h = hasher(glm::ivec2(x, z));
            auto [it, inserted] = hashes.insert(h);
            if (!inserted)
            {
                ++collisions;
            }
        }
    }

    // With 65x65 = 4225 entries, some collisions are statistically possible
    // but a good hash should have very few. Allow up to 1% collision rate.
    REQUIRE(collisions < 43); // < 1% of 4225
}

TEST_CASE("ChunkCoordHash — different coords produce different hashes", "[world][chunkmanager]")
{
    ChunkCoordHash hasher;

    // Adjacent coords should produce different hashes
    REQUIRE(hasher(glm::ivec2(0, 0)) != hasher(glm::ivec2(1, 0)));
    REQUIRE(hasher(glm::ivec2(0, 0)) != hasher(glm::ivec2(0, 1)));
    REQUIRE(hasher(glm::ivec2(1, 0)) != hasher(glm::ivec2(0, 1)));

    // Symmetric coords should produce different hashes
    REQUIRE(hasher(glm::ivec2(3, 7)) != hasher(glm::ivec2(7, 3)));
}

// ── ChunkManager lifecycle tests ────────────────────────────────────────────────

TEST_CASE("ChunkManager: load and unload lifecycle", "[world][chunkmanager]")
{
    ChunkManager mgr;

    SECTION("newly created manager has zero chunks")
    {
        REQUIRE(mgr.loadedChunkCount() == 0);
    }

    SECTION("loadChunk creates empty column")
    {
        mgr.loadChunk({0, 0});
        REQUIRE(mgr.loadedChunkCount() == 1);

        ChunkColumn* column = mgr.getChunk({0, 0});
        REQUIRE(column != nullptr);
        REQUIRE(column->getChunkCoord() == glm::ivec2(0, 0));
        REQUIRE(column->isAllEmpty());
    }

    SECTION("unloadChunk removes column")
    {
        mgr.loadChunk({3, -2});
        REQUIRE(mgr.loadedChunkCount() == 1);

        mgr.unloadChunk({3, -2});
        REQUIRE(mgr.loadedChunkCount() == 0);
        REQUIRE(mgr.getChunk({3, -2}) == nullptr);
    }

    SECTION("loadChunk is idempotent")
    {
        mgr.loadChunk({1, 1});
        ChunkColumn* first = mgr.getChunk({1, 1});

        mgr.loadChunk({1, 1});
        ChunkColumn* second = mgr.getChunk({1, 1});

        REQUIRE(mgr.loadedChunkCount() == 1);
        REQUIRE(first == second);
    }

    SECTION("unloadChunk on absent chunk is no-op")
    {
        mgr.unloadChunk({99, 99});
        REQUIRE(mgr.loadedChunkCount() == 0);
    }

    SECTION("getChunk returns nullptr for unloaded chunk")
    {
        REQUIRE(mgr.getChunk({5, 5}) == nullptr);
    }
}

// ── getBlock tests ──────────────────────────────────────────────────────────────

TEST_CASE("ChunkManager: getBlock returns AIR for unloaded chunks", "[world][chunkmanager]")
{
    ChunkManager mgr;

    REQUIRE(mgr.getBlock({0, 0, 0}) == BLOCK_AIR);
    REQUIRE(mgr.getBlock({100, 64, 200}) == BLOCK_AIR);
    REQUIRE(mgr.getBlock({-50, 128, -50}) == BLOCK_AIR);
}

TEST_CASE("ChunkManager: getBlock reads correct block from loaded chunk", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});

    // Set via ChunkColumn directly to verify getBlock translation
    ChunkColumn* column = mgr.getChunk({0, 0});
    column->setBlock(5, 10, 3, 42);

    REQUIRE(mgr.getBlock({5, 10, 3}) == 42);
}

TEST_CASE("ChunkManager: getBlock across chunk boundaries", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});
    mgr.loadChunk({1, 0});

    // worldX=15 → chunk (0,0), localX=15
    mgr.setBlock({15, 0, 0}, 100);
    // worldX=16 → chunk (1,0), localX=0
    mgr.setBlock({16, 0, 0}, 200);

    REQUIRE(mgr.getBlock({15, 0, 0}) == 100);
    REQUIRE(mgr.getBlock({16, 0, 0}) == 200);

    // Verify they are in different chunks
    REQUIRE(mgr.getChunk({0, 0})->getBlock(15, 0, 0) == 100);
    REQUIRE(mgr.getChunk({1, 0})->getBlock(0, 0, 0) == 200);
}

// ── setBlock tests ──────────────────────────────────────────────────────────────

TEST_CASE("ChunkManager: setBlock marks correct section dirty", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});

    // y=48 → section 3, localY = 48 % 16 = 0 (Y-boundary)
    // Neighbor invalidation: localY==0 && sectionY>0 → section 2 also dirty
    mgr.setBlock({5, 48, 5}, 10);

    ChunkColumn* column = mgr.getChunk({0, 0});
    REQUIRE(column->isSectionDirty(3));
    REQUIRE(column->isSectionDirty(2));     // Y-boundary neighbor invalidation
    REQUIRE_FALSE(column->isSectionDirty(0));
    REQUIRE_FALSE(column->isSectionDirty(4));
}

TEST_CASE("ChunkManager: setBlock at X boundary marks neighbor chunk dirty (AC8)", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});
    mgr.loadChunk({-1, 0});
    mgr.loadChunk({1, 0});

    SECTION("block at local x=0 marks NegX neighbor dirty")
    {
        // worldPos (0, 5, 5) → chunk (0,0), local (0, 5, 5) → x=0 boundary
        mgr.setBlock({0, 5, 5}, 10);

        REQUIRE(mgr.getChunk({0, 0})->isSectionDirty(0));
        REQUIRE(mgr.getChunk({-1, 0})->isSectionDirty(0));
        REQUIRE_FALSE(mgr.getChunk({1, 0})->isSectionDirty(0));
    }

    SECTION("block at local x=15 marks PosX neighbor dirty")
    {
        // worldPos (15, 5, 5) → chunk (0,0), local (15, 5, 5) → x=15 boundary
        mgr.setBlock({15, 5, 5}, 10);

        REQUIRE(mgr.getChunk({0, 0})->isSectionDirty(0));
        REQUIRE(mgr.getChunk({1, 0})->isSectionDirty(0));
        REQUIRE_FALSE(mgr.getChunk({-1, 0})->isSectionDirty(0));
    }
}

TEST_CASE("ChunkManager: setBlock at Z boundary marks neighbor chunk dirty (AC8)", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});
    mgr.loadChunk({0, -1});
    mgr.loadChunk({0, 1});

    SECTION("block at local z=0 marks NegZ neighbor dirty")
    {
        // worldPos (5, 5, 0) → chunk (0,0), local (5, 5, 0) → z=0 boundary
        mgr.setBlock({5, 5, 0}, 10);

        REQUIRE(mgr.getChunk({0, 0})->isSectionDirty(0));
        REQUIRE(mgr.getChunk({0, -1})->isSectionDirty(0));
        REQUIRE_FALSE(mgr.getChunk({0, 1})->isSectionDirty(0));
    }

    SECTION("block at local z=15 marks PosZ neighbor dirty")
    {
        // worldPos (5, 5, 15) → chunk (0,0), local (5, 5, 15) → z=15 boundary
        mgr.setBlock({5, 5, 15}, 10);

        REQUIRE(mgr.getChunk({0, 0})->isSectionDirty(0));
        REQUIRE(mgr.getChunk({0, 1})->isSectionDirty(0));
        REQUIRE_FALSE(mgr.getChunk({0, -1})->isSectionDirty(0));
    }
}

TEST_CASE("ChunkManager: setBlock at interior position doesn't mark neighbor chunks dirty", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});
    mgr.loadChunk({1, 0});
    mgr.loadChunk({0, 1});

    // worldPos (8, 5, 8) → chunk (0,0), local (8, 5, 8) — interior, no boundary
    mgr.setBlock({8, 5, 8}, 10);

    REQUIRE(mgr.getChunk({0, 0})->isSectionDirty(0));
    REQUIRE_FALSE(mgr.getChunk({1, 0})->isSectionDirty(0));
    REQUIRE_FALSE(mgr.getChunk({0, 1})->isSectionDirty(0));
}

TEST_CASE("ChunkManager: setBlock roundtrip with getBlock", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});

    mgr.setBlock({7, 100, 12}, 555);
    REQUIRE(mgr.getBlock({7, 100, 12}) == 555);
}

// ── Negative coordinate tests ───────────────────────────────────────────────────

TEST_CASE("ChunkManager: negative coordinate translation", "[world][chunkmanager]")
{
    ChunkManager mgr;

    SECTION("worldPos (-1, 0, 0) maps to chunk (-1, 0), local (15, 0, 0)")
    {
        mgr.loadChunk({-1, 0});
        mgr.setBlock({-1, 0, 0}, 42);

        REQUIRE(mgr.getBlock({-1, 0, 0}) == 42);
        REQUIRE(mgr.getChunk({-1, 0})->getBlock(15, 0, 0) == 42);
    }

    SECTION("worldPos (-16, 0, 0) maps to chunk (-1, 0), local (0, 0, 0)")
    {
        mgr.loadChunk({-1, 0});
        mgr.setBlock({-16, 0, 0}, 77);

        REQUIRE(mgr.getBlock({-16, 0, 0}) == 77);
        REQUIRE(mgr.getChunk({-1, 0})->getBlock(0, 0, 0) == 77);
    }

    SECTION("worldPos (-17, 0, -17) maps to chunk (-2, -2), local (15, 0, 15)")
    {
        mgr.loadChunk({-2, -2});
        mgr.setBlock({-17, 0, -17}, 99);

        REQUIRE(mgr.getBlock({-17, 0, -17}) == 99);
        REQUIRE(mgr.getChunk({-2, -2})->getBlock(15, 0, 15) == 99);
    }
}

// ── Multiple chunks ─────────────────────────────────────────────────────────────

TEST_CASE("ChunkManager: multiple chunks loaded simultaneously", "[world][chunkmanager]")
{
    ChunkManager mgr;

    mgr.loadChunk({0, 0});
    mgr.loadChunk({1, 0});
    mgr.loadChunk({0, 1});
    mgr.loadChunk({-1, -1});

    REQUIRE(mgr.loadedChunkCount() == 4);

    // Set blocks in different chunks
    mgr.setBlock({5, 10, 5}, 10);      // chunk (0,0)
    mgr.setBlock({20, 10, 5}, 20);     // chunk (1,0)
    mgr.setBlock({5, 10, 20}, 30);     // chunk (0,1)
    mgr.setBlock({-10, 10, -10}, 40);  // chunk (-1,-1)

    REQUIRE(mgr.getBlock({5, 10, 5}) == 10);
    REQUIRE(mgr.getBlock({20, 10, 5}) == 20);
    REQUIRE(mgr.getBlock({5, 10, 20}) == 30);
    REQUIRE(mgr.getBlock({-10, 10, -10}) == 40);

    // Unload one chunk, others remain
    mgr.unloadChunk({1, 0});
    REQUIRE(mgr.loadedChunkCount() == 3);
    REQUIRE(mgr.getBlock({20, 10, 5}) == BLOCK_AIR); // unloaded → AIR
    REQUIRE(mgr.getBlock({5, 10, 5}) == 10);          // still loaded
}

// ── dirtyChunkCount tests ───────────────────────────────────────────────────────

TEST_CASE("ChunkManager: dirtyChunkCount", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});
    mgr.loadChunk({1, 0});
    mgr.loadChunk({0, 1});

    SECTION("no dirty chunks initially")
    {
        REQUIRE(mgr.dirtyChunkCount() == 0);
    }

    SECTION("setBlock dirties the chunk")
    {
        mgr.setBlock({5, 10, 5}, 1);
        REQUIRE(mgr.dirtyChunkCount() == 1);
    }

    SECTION("multiple dirty chunks counted correctly")
    {
        mgr.setBlock({5, 10, 5}, 1);     // chunk (0,0)
        mgr.setBlock({20, 10, 5}, 1);    // chunk (1,0)
        REQUIRE(mgr.dirtyChunkCount() == 2);
    }

    SECTION("clearing dirty flag reduces count")
    {
        mgr.setBlock({5, 10, 5}, 1);     // chunk (0,0), section 0
        mgr.setBlock({20, 10, 5}, 1);    // chunk (1,0), section 0

        mgr.getChunk({0, 0})->clearDirty(0);
        REQUIRE(mgr.dirtyChunkCount() == 1);
    }
}

// ── const correctness ───────────────────────────────────────────────────────────

TEST_CASE("ChunkManager: const methods work on const reference", "[world][chunkmanager]")
{
    ChunkManager mgr;
    mgr.loadChunk({0, 0});
    mgr.setBlock({5, 10, 5}, 42);

    const ChunkManager& constMgr = mgr;
    REQUIRE(constMgr.getChunk({0, 0}) != nullptr);
    REQUIRE(constMgr.getChunk({99, 99}) == nullptr);
    REQUIRE(constMgr.getBlock({5, 10, 5}) == 42);
    REQUIRE(constMgr.loadedChunkCount() == 1);
    REQUIRE(constMgr.dirtyChunkCount() == 1);
}
