#include "voxel/renderer/RendererConstants.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

// CPU-only tests verifying the quad index pattern generation logic — no GPU required.

namespace
{

/// Generates index data using the same algorithm as QuadIndexBuffer::create().
std::vector<uint32_t> generateQuadIndices(uint32_t quadCount)
{
    std::vector<uint32_t> indices(static_cast<size_t>(quadCount) * 6);
    for (uint32_t q = 0; q < quadCount; ++q)
    {
        uint32_t base = q * 4;
        size_t i = static_cast<size_t>(q) * 6;
        indices[i + 0] = base + 0;
        indices[i + 1] = base + 1;
        indices[i + 2] = base + 2;
        indices[i + 3] = base + 2;
        indices[i + 4] = base + 3;
        indices[i + 5] = base + 0;
    }
    return indices;
}

} // namespace

TEST_CASE("Quad index pattern is correct for first 3 quads", "[renderer][quad-index]")
{
    auto indices = generateQuadIndices(3);
    REQUIRE(indices.size() == 18);

    // Quad 0: vertices 0,1,2,3
    CHECK(indices[0] == 0);
    CHECK(indices[1] == 1);
    CHECK(indices[2] == 2);
    CHECK(indices[3] == 2);
    CHECK(indices[4] == 3);
    CHECK(indices[5] == 0);

    // Quad 1: vertices 4,5,6,7
    CHECK(indices[6] == 4);
    CHECK(indices[7] == 5);
    CHECK(indices[8] == 6);
    CHECK(indices[9] == 6);
    CHECK(indices[10] == 7);
    CHECK(indices[11] == 4);

    // Quad 2: vertices 8,9,10,11
    CHECK(indices[12] == 8);
    CHECK(indices[13] == 9);
    CHECK(indices[14] == 10);
    CHECK(indices[15] == 10);
    CHECK(indices[16] == 11);
    CHECK(indices[17] == 8);
}

TEST_CASE("Quad index pattern scales correctly", "[renderer][quad-index]")
{
    constexpr uint32_t testQuads = 1000;
    auto indices = generateQuadIndices(testQuads);
    REQUIRE(indices.size() == testQuads * 6);

    SECTION("Last quad indices are correct")
    {
        uint32_t lastQ = testQuads - 1;
        uint32_t base = lastQ * 4;
        size_t i = static_cast<size_t>(lastQ) * 6;

        CHECK(indices[i + 0] == base + 0);
        CHECK(indices[i + 1] == base + 1);
        CHECK(indices[i + 2] == base + 2);
        CHECK(indices[i + 3] == base + 2);
        CHECK(indices[i + 4] == base + 3);
        CHECK(indices[i + 5] == base + 0);
    }

    SECTION("Every quad follows the {0,1,2, 2,3,0} pattern relative to its base")
    {
        for (uint32_t q = 0; q < testQuads; ++q)
        {
            uint32_t base = q * 4;
            size_t i = static_cast<size_t>(q) * 6;

            REQUIRE(indices[i + 0] == base + 0);
            REQUIRE(indices[i + 1] == base + 1);
            REQUIRE(indices[i + 2] == base + 2);
            REQUIRE(indices[i + 3] == base + 2);
            REQUIRE(indices[i + 4] == base + 3);
            REQUIRE(indices[i + 5] == base + 0);
        }
    }
}

TEST_CASE("MAX_QUADS and QUAD_INDEX_BUFFER_SIZE are consistent", "[renderer][quad-index]")
{
    using namespace voxel::renderer;

    CHECK(MAX_QUADS == 2'000'000);
    CHECK(QUAD_INDEX_BUFFER_SIZE == static_cast<VkDeviceSize>(MAX_QUADS) * 6 * sizeof(uint32_t));
    CHECK(QUAD_INDEX_BUFFER_SIZE == 48'000'000); // 48 MB
}
