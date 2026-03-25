// CPU-only tests for StagingBuffer logic — no GPU required.
// These tests validate ring-buffer offset tracking, rate limiting,
// input validation, and frame state management using a mock approach
// that directly tests the public API contract without Vulkan calls.

#include "voxel/core/Result.h"
#include "voxel/renderer/StagingBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using namespace voxel::renderer;

// We cannot create a real StagingBuffer without a VulkanContext + GPU,
// so we test the PendingTransfer struct and validation logic via
// a lightweight approach: construct scenarios that exercise the
// same logic the StagingBuffer uses internally.

TEST_CASE("PendingTransfer struct has correct defaults", "[renderer][staging]")
{
    PendingTransfer transfer{};
    REQUIRE(transfer.srcOffset == 0);
    REQUIRE(transfer.dstOffset == 0);
    REQUIRE(transfer.size == 0);
}

TEST_CASE("PendingTransfer stores values correctly", "[renderer][staging]")
{
    PendingTransfer transfer{
        .srcOffset = 1024,
        .dstOffset = 2048,
        .size = 512
    };

    REQUIRE(transfer.srcOffset == 1024);
    REQUIRE(transfer.dstOffset == 2048);
    REQUIRE(transfer.size == 512);
}

TEST_CASE("StagingBuffer constants have expected values", "[renderer][staging]")
{
    REQUIRE(StagingBuffer::DEFAULT_STAGING_SIZE == 16 * 1024 * 1024);
    REQUIRE(StagingBuffer::DEFAULT_MAX_UPLOADS == 8);
    REQUIRE(StagingBuffer::ALIGNMENT == 16);
}

// Ring-buffer offset tracking simulation
// Simulates the same logic as StagingBuffer::uploadToGigabuffer and beginFrame
// without requiring Vulkan objects.

namespace
{

struct RingBufferSim
{
    VkDeviceSize capacity;
    VkDeviceSize frameRegionSize;
    VkDeviceSize writeOffset = 0;
    VkDeviceSize usedBytes = 0;
    uint32_t currentFrameIndex = 0;
    uint32_t maxUploadsPerFrame = StagingBuffer::DEFAULT_MAX_UPLOADS;
    std::vector<PendingTransfer> pendingTransfers;
    std::array<bool, FRAMES_IN_FLIGHT> fenceSubmitted{};

    explicit RingBufferSim(VkDeviceSize cap = StagingBuffer::DEFAULT_STAGING_SIZE)
        : capacity(cap)
        , frameRegionSize(cap / FRAMES_IN_FLIGHT)
    {
    }

    void beginFrame(uint32_t frameIndex)
    {
        currentFrameIndex = frameIndex;
        // Real StagingBuffer only waits/resets fence if fenceSubmitted[frameIndex] is true
        if (fenceSubmitted[frameIndex])
        {
            fenceSubmitted[frameIndex] = false;
        }
        writeOffset = static_cast<VkDeviceSize>(frameIndex) * frameRegionSize;
        usedBytes = 0;
        pendingTransfers.clear();
    }

    void markFenceSubmitted()
    {
        fenceSubmitted[currentFrameIndex] = true;
    }

    voxel::core::Result<void> upload(size_t size, VkDeviceSize dstOffset)
    {
        if (size == 0)
        {
            return std::unexpected(voxel::core::EngineError::InvalidArgument);
        }

        if (pendingTransfers.size() >= maxUploadsPerFrame)
        {
            return {}; // rate-limited, deferred
        }

        VkDeviceSize alignedSize = (static_cast<VkDeviceSize>(size) + StagingBuffer::ALIGNMENT - 1)
                                 & ~(StagingBuffer::ALIGNMENT - 1);

        if (usedBytes + alignedSize > frameRegionSize)
        {
            return std::unexpected(voxel::core::EngineError::OutOfMemory);
        }

        pendingTransfers.push_back(PendingTransfer{
            .srcOffset = writeOffset,
            .dstOffset = dstOffset,
            .size = static_cast<VkDeviceSize>(size)
        });

        writeOffset += alignedSize;
        usedBytes += alignedSize;

        return {};
    }
};

} // anonymous namespace

TEST_CASE("Ring-buffer offset tracking", "[renderer][staging]")
{
    RingBufferSim sim(1024); // 1 KB total, 512 bytes per frame

    SECTION("frame 0 starts at offset 0")
    {
        sim.beginFrame(0);
        REQUIRE(sim.writeOffset == 0);
        REQUIRE(sim.usedBytes == 0);
    }

    SECTION("frame 1 starts at half capacity")
    {
        sim.beginFrame(1);
        REQUIRE(sim.writeOffset == 512);
        REQUIRE(sim.usedBytes == 0);
    }

    SECTION("upload advances write offset with alignment")
    {
        sim.beginFrame(0);
        auto result = sim.upload(100, 0);
        REQUIRE(result.has_value());
        // 100 bytes aligned up to 16 = 112
        REQUIRE(sim.writeOffset == 112);
        REQUIRE(sim.usedBytes == 112);
    }

    SECTION("multiple uploads accumulate correctly")
    {
        sim.beginFrame(0);
        REQUIRE(sim.upload(64, 0).has_value());   // aligned to 64
        REQUIRE(sim.upload(32, 100).has_value());  // aligned to 32
        REQUIRE(sim.upload(16, 200).has_value());  // aligned to 16

        REQUIRE(sim.usedBytes == 64 + 32 + 16);
        REQUIRE(sim.pendingTransfers.size() == 3);
    }

    SECTION("upload fills pending transfers correctly")
    {
        sim.beginFrame(0);
        REQUIRE(sim.upload(64, 1000).has_value());
        REQUIRE(sim.upload(32, 2000).has_value());

        REQUIRE(sim.pendingTransfers.size() == 2);
        REQUIRE(sim.pendingTransfers[0].srcOffset == 0);
        REQUIRE(sim.pendingTransfers[0].dstOffset == 1000);
        REQUIRE(sim.pendingTransfers[0].size == 64);
        REQUIRE(sim.pendingTransfers[1].srcOffset == 64);
        REQUIRE(sim.pendingTransfers[1].dstOffset == 2000);
        REQUIRE(sim.pendingTransfers[1].size == 32);
    }
}

TEST_CASE("Rate limiting rejects excess uploads", "[renderer][staging]")
{
    RingBufferSim sim(16 * 1024); // 16 KB
    sim.maxUploadsPerFrame = 3;
    sim.beginFrame(0);

    // First 3 uploads succeed
    REQUIRE(sim.upload(16, 0).has_value());
    REQUIRE(sim.upload(16, 100).has_value());
    REQUIRE(sim.upload(16, 200).has_value());
    REQUIRE(sim.pendingTransfers.size() == 3);

    // 4th upload is rate-limited (returns success but no transfer added)
    REQUIRE(sim.upload(16, 300).has_value());
    REQUIRE(sim.pendingTransfers.size() == 3); // still 3
}

TEST_CASE("beginFrame resets state for next frame slot", "[renderer][staging]")
{
    RingBufferSim sim(1024);
    sim.beginFrame(0);

    // Fill up some transfers
    sim.upload(64, 0);
    sim.upload(64, 100);
    REQUIRE(sim.pendingTransfers.size() == 2);
    REQUIRE(sim.usedBytes == 128);

    // Switch to frame 1
    sim.beginFrame(1);
    REQUIRE(sim.pendingTransfers.empty());
    REQUIRE(sim.usedBytes == 0);
    REQUIRE(sim.writeOffset == 512);

    // Switch back to frame 0
    sim.beginFrame(0);
    REQUIRE(sim.pendingTransfers.empty());
    REQUIRE(sim.usedBytes == 0);
    REQUIRE(sim.writeOffset == 0);
}

TEST_CASE("Zero-size upload rejected", "[renderer][staging]")
{
    RingBufferSim sim(1024);
    sim.beginFrame(0);

    auto result = sim.upload(0, 0);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == voxel::core::EngineError::InvalidArgument);
}

TEST_CASE("Out-of-memory when frame region is full", "[renderer][staging]")
{
    RingBufferSim sim(256); // 128 bytes per frame

    sim.beginFrame(0);

    // Use most of the region
    REQUIRE(sim.upload(112, 0).has_value()); // aligned 112

    // Try to upload more than remaining space (128 - 112 = 16 bytes left)
    auto result = sim.upload(32, 200);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == voxel::core::EngineError::OutOfMemory);
}

TEST_CASE("Frame regions do not overlap", "[renderer][staging]")
{
    RingBufferSim sim(1024);

    // Frame 0: writes at offset 0..
    sim.beginFrame(0);
    REQUIRE(sim.upload(256, 0).has_value());
    VkDeviceSize frame0End = sim.writeOffset;

    // Frame 1: writes at offset 512..
    sim.beginFrame(1);
    REQUIRE(sim.upload(256, 0).has_value());
    VkDeviceSize frame1Start = 512;

    // Frame 0's writes must not reach frame 1's region
    REQUIRE(frame0End <= frame1Start);
}

TEST_CASE("No deadlock when cycling frames without uploads", "[renderer][staging]")
{
    RingBufferSim sim(1024);

    // Simulate several frame cycles with no uploads (no flushTransfers submit).
    // Before the fix, this would deadlock on frame 3 because the fence
    // was reset but never signaled.
    for (int i = 0; i < 10; ++i)
    {
        uint32_t frameIdx = i % FRAMES_IN_FLIGHT;
        sim.beginFrame(frameIdx);
        // No uploads, no flush → fenceSubmitted stays false
        REQUIRE(sim.fenceSubmitted[frameIdx] == false);
    }
}

TEST_CASE("Fence tracking set after simulated flush", "[renderer][staging]")
{
    RingBufferSim sim(1024);

    sim.beginFrame(0);
    REQUIRE(sim.upload(64, 0).has_value());
    sim.markFenceSubmitted(); // simulates flushTransfers queue submit

    REQUIRE(sim.fenceSubmitted[0] == true);

    // Next cycle for frame 0: should clear the flag (simulating wait+reset)
    sim.beginFrame(1); // frame 1 first
    sim.beginFrame(0); // back to frame 0
    REQUIRE(sim.fenceSubmitted[0] == false);
}

TEST_CASE("Alignment rounds up correctly", "[renderer][staging]")
{
    RingBufferSim sim(4096);
    sim.beginFrame(0);

    SECTION("size 1 aligned to 16")
    {
        sim.upload(1, 0);
        REQUIRE(sim.usedBytes == 16);
    }

    SECTION("size 15 aligned to 16")
    {
        sim.upload(15, 0);
        REQUIRE(sim.usedBytes == 16);
    }

    SECTION("size 16 stays 16")
    {
        sim.upload(16, 0);
        REQUIRE(sim.usedBytes == 16);
    }

    SECTION("size 17 aligned to 32")
    {
        sim.upload(17, 0);
        REQUIRE(sim.usedBytes == 32);
    }

    SECTION("size 33 aligned to 48")
    {
        sim.upload(33, 0);
        REQUIRE(sim.usedBytes == 48);
    }
}
