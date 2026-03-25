#include "voxel/world/ChunkColumn.h"

#include <catch2/catch_test_macros.hpp>

using namespace voxel::world;

TEST_CASE("ChunkColumn", "[world]")
{
    ChunkColumn column({4, -7});

    SECTION("default construction — all sections null, no dirty flags")
    {
        REQUIRE(column.getChunkCoord() == glm::ivec2(4, -7));

        for (int i = 0; i < ChunkColumn::SECTIONS_PER_COLUMN; ++i)
        {
            REQUIRE(column.getSection(i) == nullptr);
            REQUIRE_FALSE(column.isSectionDirty(i));
        }
    }

    SECTION("getBlock on null section returns BLOCK_AIR")
    {
        REQUIRE(column.getBlock(0, 0, 0) == BLOCK_AIR);
        REQUIRE(column.getBlock(8, 100, 8) == BLOCK_AIR);
        REQUIRE(column.getBlock(15, 255, 15) == BLOCK_AIR);

        // Verify section was NOT allocated by getBlock
        REQUIRE(column.getSection(6) == nullptr);
    }

    SECTION("setBlock auto-allocates section")
    {
        REQUIRE(column.getSection(0) == nullptr);

        column.setBlock(5, 3, 7, 42);

        REQUIRE(column.getSection(0) != nullptr);
        REQUIRE(column.getBlock(5, 3, 7) == 42);
    }

    SECTION("get/set roundtrip at various Y levels")
    {
        // Section 0, y=0
        column.setBlock(0, 0, 0, 10);
        REQUIRE(column.getBlock(0, 0, 0) == 10);

        // Section 3, y=48 (48/16=3, 48%16=0)
        column.setBlock(7, 48, 3, 200);
        REQUIRE(column.getBlock(7, 48, 3) == 200);

        // Section 15, y=255 (255/16=15, 255%16=15)
        column.setBlock(15, 255, 15, 65535);
        REQUIRE(column.getBlock(15, 255, 15) == 65535);

        // Section 8, y=128 (128/16=8, 128%16=0)
        column.setBlock(0, 128, 0, 1);
        REQUIRE(column.getBlock(0, 128, 0) == 1);
    }

    SECTION("cross-section boundary y=15 and y=16")
    {
        // y=15 -> section 0, localY=15
        column.setBlock(5, 15, 5, 100);
        // y=16 -> section 1, localY=0
        column.setBlock(5, 16, 5, 200);

        REQUIRE(column.getBlock(5, 15, 5) == 100);
        REQUIRE(column.getBlock(5, 16, 5) == 200);

        // Verify they are in different sections
        ChunkSection* section0 = column.getSection(0);
        ChunkSection* section1 = column.getSection(1);
        REQUIRE(section0 != nullptr);
        REQUIRE(section1 != nullptr);
        REQUIRE(section0 != section1);

        // Verify via direct section access
        REQUIRE(section0->getBlock(5, 15, 5) == 100);
        REQUIRE(section1->getBlock(5, 0, 5) == 200);
    }

    SECTION("dirty flag set on setBlock, cleared by clearDirty")
    {
        REQUIRE_FALSE(column.isSectionDirty(3));

        // setBlock should set dirty flag for the target section
        column.setBlock(0, 48, 0, 1); // section 3
        REQUIRE(column.isSectionDirty(3));

        // Other sections should remain clean
        REQUIRE_FALSE(column.isSectionDirty(0));
        REQUIRE_FALSE(column.isSectionDirty(2));
        REQUIRE_FALSE(column.isSectionDirty(4));

        // clearDirty resets the flag
        column.clearDirty(3);
        REQUIRE_FALSE(column.isSectionDirty(3));
    }

    SECTION("dirty flag isolation — only target section is dirtied")
    {
        column.setBlock(0, 0, 0, 1);   // section 0
        column.setBlock(0, 32, 0, 1);  // section 2
        column.setBlock(0, 240, 0, 1); // section 15

        REQUIRE(column.isSectionDirty(0));
        REQUIRE_FALSE(column.isSectionDirty(1));
        REQUIRE(column.isSectionDirty(2));
        REQUIRE_FALSE(column.isSectionDirty(3));
        REQUIRE(column.isSectionDirty(15));
    }

    SECTION("getOrCreateSection idempotent — second call returns same section")
    {
        ChunkSection& first = column.getOrCreateSection(5);
        ChunkSection& second = column.getOrCreateSection(5);
        REQUIRE(&first == &second);
    }

    SECTION("getOrCreateSection allocates empty section")
    {
        REQUIRE(column.getSection(7) == nullptr);

        ChunkSection& section = column.getOrCreateSection(7);
        REQUIRE(column.getSection(7) != nullptr);
        REQUIRE(section.isEmpty());
    }

    SECTION("bounds extremes — y=0 and y=255 valid")
    {
        column.setBlock(0, 0, 0, 1);
        REQUIRE(column.getBlock(0, 0, 0) == 1);

        column.setBlock(15, 255, 15, 2);
        REQUIRE(column.getBlock(15, 255, 15) == 2);
    }

    SECTION("bounds extremes — sectionY=0 and sectionY=15 valid")
    {
        REQUIRE(column.getSection(0) == nullptr);
        REQUIRE(column.getSection(15) == nullptr);

        column.getOrCreateSection(0);
        column.getOrCreateSection(15);
        REQUIRE(column.getSection(0) != nullptr);
        REQUIRE(column.getSection(15) != nullptr);
    }

    SECTION("isAllEmpty — true for fresh column")
    {
        REQUIRE(column.isAllEmpty());
    }

    SECTION("isAllEmpty — false after setting a block")
    {
        column.setBlock(5, 100, 5, 42);
        REQUIRE_FALSE(column.isAllEmpty());
    }

    SECTION("isAllEmpty — true if allocated section has only AIR")
    {
        // Allocate section but don't write any non-air blocks
        column.getOrCreateSection(3);
        REQUIRE(column.isAllEmpty());
    }

    SECTION("getHighestNonEmptySection — returns -1 for empty column")
    {
        REQUIRE(column.getHighestNonEmptySection() == -1);
    }

    SECTION("getHighestNonEmptySection — returns correct index")
    {
        column.setBlock(0, 32, 0, 1); // section 2
        REQUIRE(column.getHighestNonEmptySection() == 2);

        column.setBlock(0, 160, 0, 1); // section 10
        REQUIRE(column.getHighestNonEmptySection() == 10);

        column.setBlock(0, 0, 0, 1); // section 0
        REQUIRE(column.getHighestNonEmptySection() == 10);
    }

    SECTION("getHighestNonEmptySection — ignores allocated but empty sections")
    {
        column.getOrCreateSection(12); // allocated but empty
        column.setBlock(0, 48, 0, 1); // section 3 has a block
        REQUIRE(column.getHighestNonEmptySection() == 3);
    }

    SECTION("COLUMN_HEIGHT constant is 256")
    {
        REQUIRE(ChunkColumn::COLUMN_HEIGHT == 256);
        REQUIRE(ChunkColumn::SECTIONS_PER_COLUMN == 16);
    }
}
