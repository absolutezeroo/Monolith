#include "voxel/world/BlockLightPropagator.h"

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

/// Run BFS within a column from an already-seeded queue. Stays within column bounds.
void bfsExpandWithinColumn(ChunkColumn& column, const BlockRegistry& registry, std::queue<LightNode>& queue)
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

            // Check target block: fully opaque blocks stop light
            const ChunkSection* nSection = column.getSection(nSectionY);
            if (nSection != nullptr)
            {
                uint16_t blockId = nSection->getBlock(nx, nLocalY, nz);
                const BlockDefinition& def = registry.getBlockType(blockId);
                if (def.lightFilter == 15)
                {
                    continue;
                }
            }

            uint8_t newLight = node.light - 1;
            if (newLight == 0)
            {
                continue;
            }

            LightMap& nLightMap = column.getLightMap(nSectionY);
            uint8_t existing = nLightMap.getBlockLight(nx, nLocalY, nz);
            if (newLight > existing)
            {
                nLightMap.setBlockLight(nx, nLocalY, nz, newLight);
                queue.push(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), newLight});
            }
        }
    }
}

} // anonymous namespace

void BlockLightPropagator::propagateColumn(ChunkColumn& column, const BlockRegistry& registry)
{
    std::queue<LightNode> queue;

    // Seed: scan all sections for light-emitting blocks
    for (int sectionY = 0; sectionY < ChunkColumn::SECTIONS_PER_COLUMN; ++sectionY)
    {
        const ChunkSection* section = column.getSection(sectionY);
        if (section == nullptr)
        {
            continue;
        }

        LightMap& lightMap = column.getLightMap(sectionY);
        for (int y = 0; y < ChunkSection::SIZE; ++y)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                for (int x = 0; x < ChunkSection::SIZE; ++x)
                {
                    uint16_t blockId = section->getBlock(x, y, z);
                    const BlockDefinition& def = registry.getBlockType(blockId);
                    if (def.lightEmission > 0)
                    {
                        lightMap.setBlockLight(x, y, z, def.lightEmission);
                        int worldY = sectionY * ChunkSection::SIZE + y;
                        queue.push({static_cast<int16_t>(x), static_cast<int16_t>(worldY),
                                    static_cast<int16_t>(z), def.lightEmission});
                    }
                }
            }
        }
    }

    // BFS expand within this column
    bfsExpandWithinColumn(column, registry, queue);
}

void BlockLightPropagator::propagateBorders(ChunkColumn& column, ChunkManager& manager,
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

        // Collect seeds for batch BFS into neighbor and into our column
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

                    uint8_t ourBlockLight = ourLight.getBlockLight(ourX, y, ourZ);
                    uint8_t neighborBlockLight = neighborLight.getBlockLight(adjX, y, adjZ);
                    int worldY = sectionY * ChunkSection::SIZE + y;

                    // Push: our light → neighbor
                    if (ourBlockLight > 1 && ourBlockLight - 1 > neighborBlockLight)
                    {
                        const ChunkSection* nSec = neighborCol->getSection(sectionY);
                        bool isOpaque = false;
                        if (nSec != nullptr)
                        {
                            uint16_t blockId = nSec->getBlock(adjX, y, adjZ);
                            isOpaque = registry.getBlockType(blockId).lightFilter == 15;
                        }
                        if (!isOpaque)
                        {
                            uint8_t newVal = static_cast<uint8_t>(ourBlockLight - 1);
                            neighborLight.setBlockLight(adjX, y, adjZ, newVal);
                            neighborSeeds.push({static_cast<int16_t>(adjX), static_cast<int16_t>(worldY),
                                                static_cast<int16_t>(adjZ), newVal});
                            neighborCol->markDirty(sectionY);
                        }
                    }

                    // Pull: neighbor light → our column
                    if (neighborBlockLight > 1 && neighborBlockLight - 1 > ourBlockLight)
                    {
                        const ChunkSection* ourSec = column.getSection(sectionY);
                        bool isOpaque = false;
                        if (ourSec != nullptr)
                        {
                            uint16_t blockId = ourSec->getBlock(ourX, y, ourZ);
                            isOpaque = registry.getBlockType(blockId).lightFilter == 15;
                        }
                        if (!isOpaque)
                        {
                            uint8_t newVal = static_cast<uint8_t>(neighborBlockLight - 1);
                            ourLight.setBlockLight(ourX, y, ourZ, newVal);
                            ourSeeds.push({static_cast<int16_t>(ourX), static_cast<int16_t>(worldY),
                                           static_cast<int16_t>(ourZ), newVal});
                            column.markDirty(sectionY);
                        }
                    }
                }
            }
        }

        // Run batch BFS for all seeds
        if (!neighborSeeds.empty())
        {
            bfsExpandWithinColumn(*neighborCol, registry, neighborSeeds);
        }
        if (!ourSeeds.empty())
        {
            bfsExpandWithinColumn(column, registry, ourSeeds);
        }
    }
}

} // namespace voxel::world
