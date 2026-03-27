#include "voxel/renderer/ChunkRenderInfo.h"
#include "voxel/renderer/ChunkUploadManager.h"
#include "voxel/renderer/Gigabuffer.h"
#include "voxel/renderer/RendererConstants.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace voxel::renderer;

// =============================================================================
// ChunkRenderInfo type tests (CPU-only, no Vulkan)
// =============================================================================

TEST_CASE("SectionKey equality and hashing", "[renderer][chunk-upload]")
{
    SectionKey a{glm::ivec2{1, 2}, 3};
    SectionKey b{glm::ivec2{1, 2}, 3};
    SectionKey c{glm::ivec2{1, 2}, 4};
    SectionKey d{glm::ivec2{0, 2}, 3};

    SECTION("equal keys compare equal")
    {
        CHECK(a == b);
    }

    SECTION("different sectionY compares not equal")
    {
        CHECK_FALSE(a == c);
    }

    SECTION("different coord compares not equal")
    {
        CHECK_FALSE(a == d);
    }

    SECTION("hash produces same value for equal keys")
    {
        SectionKeyHash hash;
        CHECK(hash(a) == hash(b));
    }

    SECTION("hash produces different values for different keys")
    {
        SectionKeyHash hash;
        // Not guaranteed, but highly likely for good hashes
        CHECK(hash(a) != hash(c));
        CHECK(hash(a) != hash(d));
    }
}

TEST_CASE("ChunkRenderInfo default state", "[renderer][chunk-upload]")
{
    ChunkRenderInfo info;
    CHECK(info.quadCount == 0);
    CHECK(info.state == RenderState::None);
    CHECK(info.worldBasePos == glm::ivec3{0});
}

TEST_CASE("ChunkRenderInfoMap insert and lookup", "[renderer][chunk-upload]")
{
    ChunkRenderInfoMap map;

    SectionKey key{glm::ivec2{5, -3}, 7};
    ChunkRenderInfo info;
    info.quadCount = 100;
    info.state = RenderState::Resident;
    info.worldBasePos = glm::ivec3{80, 112, -48};

    map[key] = info;

    auto it = map.find(key);
    REQUIRE(it != map.end());
    CHECK(it->second.quadCount == 100);
    CHECK(it->second.state == RenderState::Resident);
    CHECK(it->second.worldBasePos == glm::ivec3{80, 112, -48});
}

// =============================================================================
// Deferred free queue tests (using VmaVirtualBlock directly)
// =============================================================================

TEST_CASE("DeferredFree FIFO ordering — freed after FRAMES_IN_FLIGHT ticks", "[renderer][chunk-upload]")
{
    // Simulate a VmaVirtualBlock to track allocations
    VmaVirtualBlockCreateInfo vbCI{};
    vbCI.size = 4096;
    VmaVirtualBlock block = VK_NULL_HANDLE;
    REQUIRE(vmaCreateVirtualBlock(&vbCI, &block) == VK_SUCCESS);

    // Allocate 3 sub-regions
    struct Alloc
    {
        VmaVirtualAllocation handle;
        VkDeviceSize offset;
    };
    std::vector<Alloc> allocs(3);

    for (int i = 0; i < 3; ++i)
    {
        VmaVirtualAllocationCreateInfo vaCI{};
        vaCI.size = 128;
        vaCI.alignment = 16;
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocs[i].handle, &allocs[i].offset) == VK_SUCCESS);
    }

    // Verify 3 allocations
    VmaStatistics stats{};
    vmaGetVirtualBlockStatistics(block, &stats);
    REQUIRE(stats.allocationCount == 3);

    // Simulate deferred free queue
    struct DeferredFreeEntry
    {
        VmaVirtualAllocation handle;
        uint32_t framesRemaining;
    };
    std::vector<DeferredFreeEntry> deferredFrees;

    // Queue all 3 for deferred free with FRAMES_IN_FLIGHT=2
    for (auto& a : allocs)
    {
        deferredFrees.push_back({a.handle, FRAMES_IN_FLIGHT});
    }

    SECTION("nothing freed on first tick")
    {
        // Tick once — decrement first, then check (matches processDeferredFrees logic)
        for (auto& df : deferredFrees)
        {
            --df.framesRemaining;
        }

        // After one tick, framesRemaining should be 1 (not freed yet)
        for (const auto& df : deferredFrees)
        {
            CHECK(df.framesRemaining == 1);
        }
        vmaGetVirtualBlockStatistics(block, &stats);
        CHECK(stats.allocationCount == 3);
    }

    SECTION("all freed after exactly FRAMES_IN_FLIGHT ticks")
    {
        // Tick FRAMES_IN_FLIGHT times using the actual decrement-then-check pattern
        for (uint32_t frame = 0; frame < FRAMES_IN_FLIGHT; ++frame)
        {
            for (int i = static_cast<int>(deferredFrees.size()) - 1; i >= 0; --i)
            {
                --deferredFrees[i].framesRemaining;
                if (deferredFrees[i].framesRemaining == 0)
                {
                    vmaVirtualFree(block, deferredFrees[i].handle);
                    deferredFrees[i] = deferredFrees.back();
                    deferredFrees.pop_back();
                }
            }
        }

        // All should be freed after exactly FRAMES_IN_FLIGHT ticks
        CHECK(deferredFrees.empty());
        vmaGetVirtualBlockStatistics(block, &stats);
        CHECK(stats.allocationCount == 0);
    }

    // Cleanup any remaining allocations (if test section didn't free them)
    for (const auto& df : deferredFrees)
    {
        vmaVirtualFree(block, df.handle);
    }
    vmaDestroyVirtualBlock(block);
}

TEST_CASE("ChunkRenderInfo lifecycle — None to Resident to freed", "[renderer][chunk-upload]")
{
    ChunkRenderInfoMap map;
    SectionKey key{glm::ivec2{0, 0}, 5};

    // 1. Initially None
    map[key] = ChunkRenderInfo{.state = RenderState::None};
    CHECK(map[key].state == RenderState::None);

    // 2. After upload — Resident
    VmaVirtualBlockCreateInfo vbCI{};
    vbCI.size = 4096;
    VmaVirtualBlock vblock = VK_NULL_HANDLE;
    REQUIRE(vmaCreateVirtualBlock(&vbCI, &vblock) == VK_SUCCESS);

    VmaVirtualAllocationCreateInfo vaCI{};
    vaCI.size = 256;
    vaCI.alignment = 16;
    VmaVirtualAllocation valloc;
    VkDeviceSize offset = 0;
    REQUIRE(vmaVirtualAllocate(vblock, &vaCI, &valloc, &offset) == VK_SUCCESS);

    map[key] = ChunkRenderInfo{
        .allocation = GigabufferAllocation{.offset = offset, .size = 256, .handle = valloc},
        .quadCount = 32,
        .worldBasePos = glm::ivec3{0, 80, 0},
        .state = RenderState::Resident,
    };
    CHECK(map[key].state == RenderState::Resident);
    CHECK(map[key].quadCount == 32);

    // 3. After deferred free completes — freed, remove from map
    vmaVirtualFree(vblock, valloc);
    map.erase(key);
    CHECK(map.find(key) == map.end());

    vmaDestroyVirtualBlock(vblock);
}

TEST_CASE("Empty mesh (quadCount=0) stores None state, no gigabuffer allocation", "[renderer][chunk-upload]")
{
    VmaVirtualBlockCreateInfo vbCI{};
    vbCI.size = 4096;
    VmaVirtualBlock vblock = VK_NULL_HANDLE;
    REQUIRE(vmaCreateVirtualBlock(&vbCI, &vblock) == VK_SUCCESS);

    ChunkRenderInfoMap map;
    SectionKey key{glm::ivec2{2, 3}, 0};

    // Simulate empty mesh — store None, do NOT allocate
    ChunkMesh emptyMesh;
    emptyMesh.quadCount = 0;

    if (emptyMesh.isEmpty())
    {
        map[key] = ChunkRenderInfo{.state = RenderState::None};
    }

    CHECK(map[key].state == RenderState::None);
    CHECK(map[key].quadCount == 0);

    // Verify no allocation was made
    VmaStatistics stats{};
    vmaGetVirtualBlockStatistics(vblock, &stats);
    CHECK(stats.allocationCount == 0);

    vmaDestroyVirtualBlock(vblock);
}

TEST_CASE("Upload queue drains N per frame", "[renderer][chunk-upload]")
{
    // Simulate pending uploads with priority sorting
    struct FakePendingUpload
    {
        SectionKey key;
        float distanceSq;
    };

    std::vector<FakePendingUpload> pendingUploads;
    constexpr uint32_t MAX_UPLOADS = ChunkUploadManager::MAX_UPLOADS_PER_FRAME;

    // Add 20 pending uploads at varying distances
    for (int i = 0; i < 20; ++i)
    {
        pendingUploads.push_back({
            SectionKey{glm::ivec2{i, 0}, 0},
            static_cast<float>(i * 100),
        });
    }

    CHECK(pendingUploads.size() == 20);

    // Sort by distance
    std::sort(pendingUploads.begin(), pendingUploads.end(), [](const auto& a, const auto& b) {
        return a.distanceSq < b.distanceSq;
    });

    // Drain MAX_UPLOADS_PER_FRAME
    uint32_t uploaded = 0;
    auto it = pendingUploads.begin();
    while (it != pendingUploads.end() && uploaded < MAX_UPLOADS)
    {
        it = pendingUploads.erase(it);
        ++uploaded;
    }

    CHECK(uploaded == MAX_UPLOADS);
    CHECK(pendingUploads.size() == 12); // 20 - 8 = 12 remaining

    // Verify closest were uploaded first (remaining start at distance index 8)
    CHECK(pendingUploads.front().distanceSq == 800.0f);
}

TEST_CASE("Remesh — old allocation deferred-freed, new allocation at different offset", "[renderer][chunk-upload]")
{
    VmaVirtualBlockCreateInfo vbCI{};
    vbCI.size = 4096;
    VmaVirtualBlock vblock = VK_NULL_HANDLE;
    REQUIRE(vmaCreateVirtualBlock(&vbCI, &vblock) == VK_SUCCESS);

    ChunkRenderInfoMap map;
    SectionKey key{glm::ivec2{1, 1}, 3};

    // First allocation (initial mesh)
    VmaVirtualAllocationCreateInfo vaCI{};
    vaCI.size = 128;
    vaCI.alignment = 16;
    VmaVirtualAllocation alloc1;
    VkDeviceSize offset1 = 0;
    REQUIRE(vmaVirtualAllocate(vblock, &vaCI, &alloc1, &offset1) == VK_SUCCESS);

    map[key] = ChunkRenderInfo{
        .allocation = GigabufferAllocation{.offset = offset1, .size = 128, .handle = alloc1},
        .quadCount = 16,
        .state = RenderState::Resident,
    };

    // Remesh — allocate new, defer-free old
    VmaVirtualAllocation alloc2;
    VkDeviceSize offset2 = 0;
    vaCI.size = 256; // larger remesh
    REQUIRE(vmaVirtualAllocate(vblock, &vaCI, &alloc2, &offset2) == VK_SUCCESS);

    // Old allocation queued for deferred free (simulated)
    GigabufferAllocation oldAlloc = map[key].allocation;

    // Update render info with new allocation
    map[key] = ChunkRenderInfo{
        .allocation = GigabufferAllocation{.offset = offset2, .size = 256, .handle = alloc2},
        .quadCount = 32,
        .state = RenderState::Resident,
    };

    VmaStatistics stats{};
    vmaGetVirtualBlockStatistics(vblock, &stats);
    CHECK(stats.allocationCount == 2); // Both still alive

    // New allocation is at a different offset
    CHECK(offset2 != offset1);
    CHECK(map[key].quadCount == 32);

    // Simulate deferred free of old after FRAMES_IN_FLIGHT
    vmaVirtualFree(vblock, oldAlloc.handle);
    vmaGetVirtualBlockStatistics(vblock, &stats);
    CHECK(stats.allocationCount == 1); // Only new allocation remains

    vmaVirtualFree(vblock, alloc2);
    vmaDestroyVirtualBlock(vblock);
}

TEST_CASE("Unload removes all sections and queues deferred frees", "[renderer][chunk-upload]")
{
    ChunkRenderInfoMap map;
    glm::ivec2 chunkCoord{5, 10};

    // Create render infos for several sections of the same chunk
    for (int s = 0; s < 4; ++s)
    {
        SectionKey key{chunkCoord, s};
        map[key] = ChunkRenderInfo{
            .quadCount = static_cast<uint32_t>(s * 10 + 10),
            .state = (s % 2 == 0) ? RenderState::Resident : RenderState::None,
        };
    }

    CHECK(map.size() == 4);

    // Simulate onChunkUnloaded: collect Resident allocations for deferred free, erase all
    std::vector<SectionKey> toDefer;
    for (int s = 0; s < 16; ++s)
    {
        SectionKey key{chunkCoord, s};
        auto it = map.find(key);
        if (it != map.end())
        {
            if (it->second.state == RenderState::Resident)
            {
                toDefer.push_back(key);
            }
            map.erase(it);
        }
    }

    CHECK(map.empty());
    CHECK(toDefer.size() == 2); // sections 0 and 2 were Resident
}
