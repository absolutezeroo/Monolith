#include "voxel/world/SkyLightPropagator.h"

#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/LightMap.h"

#include <queue>

namespace voxel::world
{

namespace
{

struct LightNode
{
    int16_t x; // Local X within column [0..15]
    int16_t y; // World Y [0..255]
    int16_t z; // Local Z within column [0..15]
    uint8_t light;
};

constexpr int OFFSETS[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

/// Returns true if the block at (x, localY, z) in the given section is opaque (lightFilter == 15).
/// Null sections (all air) are treated as transparent.
bool isOpaque(const ChunkColumn& column, const BlockRegistry& registry, int sectionY, int x, int localY, int z)
{
    const ChunkSection* section = column.getSection(sectionY);
    if (section == nullptr)
    {
        return false; // null section = all air = transparent
    }
    uint16_t blockId = section->getBlock(x, localY, z);
    const BlockDefinition& def = registry.getBlockType(blockId);
    return def.lightFilter == 15;
}

/// Run sky light BFS within a column from an already-seeded queue.
/// Key difference from block light: downward propagation does NOT attenuate.
void bfsSkyWithinColumn(ChunkColumn& column, const BlockRegistry& registry, std::queue<LightNode>& queue)
{
    while (!queue.empty())
    {
        LightNode node = queue.front();
        queue.pop();

        for (const auto& offset : OFFSETS)
        {
            int nx = node.x + offset[0];
            int ny = node.y + offset[1];
            int nz = node.z + offset[2];

            // World Y bounds
            if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
            {
                continue;
            }

            // Out of column X/Z bounds — cross-chunk, handled by propagateBorders
            if (nx < 0 || nx >= ChunkSection::SIZE || nz < 0 || nz >= ChunkSection::SIZE)
            {
                continue;
            }

            int nSectionY = ny / ChunkSection::SIZE;
            int nLocalY = ny % ChunkSection::SIZE;

            // Check target block: fully opaque blocks stop sky light
            if (isOpaque(column, registry, nSectionY, nx, nLocalY, nz))
            {
                continue;
            }

            // Downward propagation: no attenuation. Horizontal/upward: -1 per step.
            bool isDownward = (offset[1] == -1);
            uint8_t newLight = isDownward ? node.light : static_cast<uint8_t>(node.light - 1);
            if (newLight == 0)
            {
                continue;
            }

            LightMap& nLightMap = column.getLightMap(nSectionY);
            uint8_t existing = nLightMap.getSkyLight(nx, nLocalY, nz);
            if (newLight > existing)
            {
                nLightMap.setSkyLight(nx, nLocalY, nz, newLight);
                queue.push(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), newLight});
            }
        }
    }
}

} // anonymous namespace

void SkyLightPropagator::propagateColumn(ChunkColumn& column, const BlockRegistry& registry)
{
    std::queue<LightNode> queue;

    // Phase 1 — Seed: for each (x,z) column, scan top-down and set sky=15 for all transparent blocks
    // above (and including) the highest opaque block's position + 1.
    for (int z = 0; z < ChunkSection::SIZE; ++z)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
            {
                int sectionY = y / ChunkSection::SIZE;
                int localY = y % ChunkSection::SIZE;

                if (isOpaque(column, registry, sectionY, x, localY, z))
                {
                    break; // Hit opaque block — stop seeding this column
                }

                LightMap& lightMap = column.getLightMap(sectionY);
                lightMap.setSkyLight(x, localY, z, 15);
            }
        }
    }

    // Phase 2 — BFS: queue all sky-lit blocks at level 15 that have a non-sky-lit neighbor.
    // This propagates sky light horizontally and downward under overhangs.
    for (int z = 0; z < ChunkSection::SIZE; ++z)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
            {
                int sectionY = y / ChunkSection::SIZE;
                int localY = y % ChunkSection::SIZE;

                uint8_t skyLight = column.getLightMap(sectionY).getSkyLight(x, localY, z);
                if (skyLight != 15)
                {
                    continue;
                }

                // Check if any neighbor has lower sky light (needs propagation)
                bool hasDarkNeighbor = false;
                for (const auto& offset : OFFSETS)
                {
                    int nx = x + offset[0];
                    int ny = y + offset[1];
                    int nz = z + offset[2];

                    if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
                    {
                        continue;
                    }
                    if (nx < 0 || nx >= ChunkSection::SIZE || nz < 0 || nz >= ChunkSection::SIZE)
                    {
                        continue;
                    }

                    int nSectionY = ny / ChunkSection::SIZE;
                    int nLocalY = ny % ChunkSection::SIZE;

                    if (isOpaque(column, registry, nSectionY, nx, nLocalY, nz))
                    {
                        continue;
                    }

                    uint8_t neighborSky = column.getLightMap(nSectionY).getSkyLight(nx, nLocalY, nz);
                    if (neighborSky < 15)
                    {
                        hasDarkNeighbor = true;
                        break;
                    }
                }

                if (hasDarkNeighbor)
                {
                    queue.push({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z), 15});
                }
            }
        }
    }

    bfsSkyWithinColumn(column, registry, queue);
}

void SkyLightPropagator::propagateBorders(ChunkColumn& column, ChunkManager& manager,
                                           const BlockRegistry& registry)
{
    glm::ivec2 coord = column.getChunkCoord();

    struct BorderInfo
    {
        glm::ivec2 dCoord;
        int axis;   // 0=X border, 1=Z border
        int ourVal; // Our border coordinate value
        int adjVal; // Neighbor's adjacent coordinate value
    };

    static constexpr int LAST = ChunkSection::SIZE - 1;
    static constexpr BorderInfo borders[4] = {
        {{-1, 0}, 0, 0, LAST},  // our X=0 → neighbor at coord-1, neighbor X=15
        {{1, 0}, 0, LAST, 0},   // our X=15 → neighbor at coord+1, neighbor X=0
        {{0, -1}, 1, 0, LAST},  // our Z=0 → neighbor at coord-1, neighbor Z=15
        {{0, 1}, 1, LAST, 0},   // our Z=15 → neighbor at coord+1, neighbor Z=0
    };

    for (const auto& border : borders)
    {
        glm::ivec2 neighborCoord = coord + border.dCoord;
        ChunkColumn* neighborCol = manager.getChunk(neighborCoord);
        if (neighborCol == nullptr)
        {
            continue;
        }

        std::queue<LightNode> neighborSeeds;
        std::queue<LightNode> ourSeeds;

        for (int sectionY = 0; sectionY < ChunkColumn::SECTIONS_PER_COLUMN; ++sectionY)
        {
            LightMap& ourLight = column.getLightMap(sectionY);
            LightMap& neighborLight = neighborCol->getLightMap(sectionY);

            for (int y = 0; y < ChunkSection::SIZE; ++y)
            {
                for (int other = 0; other < ChunkSection::SIZE; ++other)
                {
                    int ourX = (border.axis == 0) ? border.ourVal : other;
                    int ourZ = (border.axis == 0) ? other : border.ourVal;
                    int adjX = (border.axis == 0) ? border.adjVal : other;
                    int adjZ = (border.axis == 0) ? other : border.adjVal;

                    uint8_t ourSkyLight = ourLight.getSkyLight(ourX, y, ourZ);
                    uint8_t neighborSkyLight = neighborLight.getSkyLight(adjX, y, adjZ);
                    int worldY = sectionY * ChunkSection::SIZE + y;

                    // Push: our sky light → neighbor
                    if (ourSkyLight > 1 && ourSkyLight - 1 > neighborSkyLight)
                    {
                        if (!isOpaque(*neighborCol, registry, sectionY, adjX, y, adjZ))
                        {
                            uint8_t newVal = static_cast<uint8_t>(ourSkyLight - 1);
                            neighborLight.setSkyLight(adjX, y, adjZ, newVal);
                            neighborSeeds.push({static_cast<int16_t>(adjX), static_cast<int16_t>(worldY),
                                                static_cast<int16_t>(adjZ), newVal});
                            neighborCol->markDirty(sectionY);
                        }
                    }

                    // Pull: neighbor sky light → our column
                    if (neighborSkyLight > 1 && neighborSkyLight - 1 > ourSkyLight)
                    {
                        if (!isOpaque(column, registry, sectionY, ourX, y, ourZ))
                        {
                            uint8_t newVal = static_cast<uint8_t>(neighborSkyLight - 1);
                            ourLight.setSkyLight(ourX, y, ourZ, newVal);
                            ourSeeds.push({static_cast<int16_t>(ourX), static_cast<int16_t>(worldY),
                                           static_cast<int16_t>(ourZ), newVal});
                            column.markDirty(sectionY);
                        }
                    }
                }
            }
        }

        if (!neighborSeeds.empty())
        {
            bfsSkyWithinColumn(*neighborCol, registry, neighborSeeds);
        }
        if (!ourSeeds.empty())
        {
            bfsSkyWithinColumn(column, registry, ourSeeds);
        }
    }
}

} // namespace voxel::world
