#include <vk_mem_alloc.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

// CPU-only tests exercising VmaVirtualBlock directly — no GPU required.

TEST_CASE("Gigabuffer virtual block basic allocation", "[renderer][gigabuffer]")
{
    VmaVirtualBlockCreateInfo vbCI{};
    vbCI.size = 1024;
    VmaVirtualBlock block = VK_NULL_HANDLE;
    REQUIRE(vmaCreateVirtualBlock(&vbCI, &block) == VK_SUCCESS);

    SECTION("allocate returns valid offset")
    {
        VmaVirtualAllocationCreateInfo vaCI{};
        vaCI.size = 128;
        vaCI.alignment = 16;

        VmaVirtualAllocation allocation;
        VkDeviceSize offset = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocation, &offset) == VK_SUCCESS);

        // First allocation should start at offset 0
        REQUIRE(offset == 0);

        // Statistics should reflect the allocation
        VmaStatistics stats{};
        vmaGetVirtualBlockStatistics(block, &stats);
        REQUIRE(stats.allocationCount == 1);
        REQUIRE(stats.allocationBytes >= 128);

        vmaVirtualFree(block, allocation);
    }

    SECTION("free and reuse returns same offset")
    {
        VmaVirtualAllocationCreateInfo vaCI{};
        vaCI.size = 128;
        vaCI.alignment = 16;

        // Allocate A
        VmaVirtualAllocation allocA;
        VkDeviceSize offsetA = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocA, &offsetA) == VK_SUCCESS);

        // Free A
        vmaVirtualFree(block, allocA);

        // Allocate B (same size) — should reuse A's offset
        VmaVirtualAllocation allocB;
        VkDeviceSize offsetB = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocB, &offsetB) == VK_SUCCESS);
        REQUIRE(offsetB == offsetA);

        vmaVirtualFree(block, allocB);
    }

    SECTION("out of memory when block is full")
    {
        // Create a tiny block
        VmaVirtualBlockCreateInfo tinyCI{};
        tinyCI.size = 256;
        VmaVirtualBlock tinyBlock = VK_NULL_HANDLE;
        REQUIRE(vmaCreateVirtualBlock(&tinyCI, &tinyBlock) == VK_SUCCESS);

        VmaVirtualAllocationCreateInfo vaCI{};
        vaCI.size = 200;
        vaCI.alignment = 1;

        VmaVirtualAllocation alloc1;
        VkDeviceSize offset1 = 0;
        REQUIRE(vmaVirtualAllocate(tinyBlock, &vaCI, &alloc1, &offset1) == VK_SUCCESS);

        // Second allocation should fail — only 56 bytes remaining
        VmaVirtualAllocationCreateInfo vaCI2{};
        vaCI2.size = 100;
        vaCI2.alignment = 1;

        VmaVirtualAllocation alloc2;
        VkDeviceSize offset2 = 0;
        REQUIRE(vmaVirtualAllocate(tinyBlock, &vaCI2, &alloc2, &offset2) == VK_ERROR_OUT_OF_DEVICE_MEMORY);

        vmaVirtualFree(tinyBlock, alloc1);
        vmaDestroyVirtualBlock(tinyBlock);
    }

    SECTION("alignment is respected")
    {
        VmaVirtualAllocationCreateInfo vaCI1{};
        vaCI1.size = 33;
        vaCI1.alignment = 16;

        VmaVirtualAllocation alloc1;
        VkDeviceSize offset1 = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI1, &alloc1, &offset1) == VK_SUCCESS);
        REQUIRE(offset1 % 16 == 0);

        // Second allocation must also be 16-byte aligned
        VmaVirtualAllocation alloc2;
        VkDeviceSize offset2 = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI1, &alloc2, &offset2) == VK_SUCCESS);
        REQUIRE(offset2 % 16 == 0);
        // offset2 must be at least 33 bytes after offset1 (rounded up to alignment)
        REQUIRE(offset2 >= offset1 + 33);

        vmaVirtualFree(block, alloc1);
        vmaVirtualFree(block, alloc2);
    }

    SECTION("multiple allocations return non-overlapping ranges")
    {
        VmaVirtualAllocationCreateInfo vaCI{};
        vaCI.size = 100;
        vaCI.alignment = 1;

        VmaVirtualAllocation allocA;
        VkDeviceSize offsetA = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocA, &offsetA) == VK_SUCCESS);

        VmaVirtualAllocation allocB;
        VkDeviceSize offsetB = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocB, &offsetB) == VK_SUCCESS);

        // Ranges [offsetA, offsetA+100) and [offsetB, offsetB+100) must not overlap
        bool noOverlap = (offsetA + 100 <= offsetB) || (offsetB + 100 <= offsetA);
        REQUIRE(noOverlap);

        vmaVirtualFree(block, allocA);
        vmaVirtualFree(block, allocB);
    }

    SECTION("statistics correct after alloc and free")
    {
        VmaVirtualAllocationCreateInfo vaCI{};
        vaCI.size = 64;
        vaCI.alignment = 1;

        VmaVirtualAllocation alloc1, alloc2, alloc3;
        VkDeviceSize offset = 0;

        REQUIRE(vmaVirtualAllocate(block, &vaCI, &alloc1, &offset) == VK_SUCCESS);
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &alloc2, &offset) == VK_SUCCESS);
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &alloc3, &offset) == VK_SUCCESS);

        VmaStatistics stats{};
        vmaGetVirtualBlockStatistics(block, &stats);
        REQUIRE(stats.allocationCount == 3);
        REQUIRE(stats.allocationBytes >= 192); // 3 * 64

        // Free one, check counts update
        vmaVirtualFree(block, alloc2);
        vmaGetVirtualBlockStatistics(block, &stats);
        REQUIRE(stats.allocationCount == 2);
        REQUIRE(stats.allocationBytes >= 128); // 2 * 64

        vmaVirtualFree(block, alloc1);
        vmaVirtualFree(block, alloc3);

        vmaGetVirtualBlockStatistics(block, &stats);
        REQUIRE(stats.allocationCount == 0);
        REQUIRE(stats.allocationBytes == 0);
    }

    SECTION("fragmentation — allocate A,B,C, free B, allocate D fits in freed space")
    {
        VmaVirtualAllocationCreateInfo vaCI{};
        vaCI.size = 64;
        vaCI.alignment = 1;

        VmaVirtualAllocation allocA, allocB, allocC;
        VkDeviceSize offsetA = 0, offsetB = 0, offsetC = 0;

        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocA, &offsetA) == VK_SUCCESS);
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocB, &offsetB) == VK_SUCCESS);
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocC, &offsetC) == VK_SUCCESS);

        VmaStatistics statsBefore{};
        vmaGetVirtualBlockStatistics(block, &statsBefore);
        REQUIRE(statsBefore.allocationCount == 3);

        // Free B — creates gap
        vmaVirtualFree(block, allocB);

        VmaStatistics statsAfterFree{};
        vmaGetVirtualBlockStatistics(block, &statsAfterFree);
        REQUIRE(statsAfterFree.allocationCount == 2);

        // Allocate D (same size as B) — should succeed using freed space
        VmaVirtualAllocation allocD;
        VkDeviceSize offsetD = 0;
        REQUIRE(vmaVirtualAllocate(block, &vaCI, &allocD, &offsetD) == VK_SUCCESS);

        // D must not overlap with A or C
        bool noOverlapA = (offsetD + 64 <= offsetA) || (offsetA + 64 <= offsetD);
        bool noOverlapC = (offsetD + 64 <= offsetC) || (offsetC + 64 <= offsetD);
        REQUIRE(noOverlapA);
        REQUIRE(noOverlapC);

        // Back to 3 allocations
        VmaStatistics statsAfterRealloc{};
        vmaGetVirtualBlockStatistics(block, &statsAfterRealloc);
        REQUIRE(statsAfterRealloc.allocationCount == 3);

        vmaVirtualFree(block, allocA);
        vmaVirtualFree(block, allocC);
        vmaVirtualFree(block, allocD);
    }

    vmaDestroyVirtualBlock(block);
}
