#include "voxel/world/DynamicLightUpdater.h"

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

constexpr int OFFSETS[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

constexpr uint8_t MAX_LIGHT_LEVEL = 15;

/// Resolve a world position to (column, localX, sectionY, localY, localZ).
/// Returns nullptr if the chunk is not loaded.
struct ResolvedPos
{
    ChunkColumn* column;
    int localX;
    int sectionY;
    int localY;
    int localZ;
};

ResolvedPos resolveWorldPos(int worldX, int worldY, int worldZ, ChunkManager& manager)
{
    glm::ivec3 worldPos{worldX, worldY, worldZ};
    glm::ivec2 chunkCoord = worldToChunkCoord(worldPos);
    ChunkColumn* col = manager.getChunk(chunkCoord);
    if (col == nullptr)
    {
        return {nullptr, 0, 0, 0, 0};
    }
    glm::ivec3 local = worldToLocalPos(worldPos);
    int sectionY = worldY / ChunkSection::SIZE;
    int localY = worldY % ChunkSection::SIZE;
    return {col, local.x, sectionY, localY, local.z};
}

/// Check if a block at the given world position is opaque (lightFilter == 15).
bool isBlockOpaque(int worldX, int worldY, int worldZ, ChunkManager& manager, const BlockRegistry& registry)
{
    if (worldY < 0 || worldY >= ChunkColumn::COLUMN_HEIGHT)
    {
        return false;
    }
    auto resolved = resolveWorldPos(worldX, worldY, worldZ, manager);
    if (resolved.column == nullptr)
    {
        return true; // Treat unloaded chunks as opaque (conservative)
    }
    const ChunkSection* section = resolved.column->getSection(resolved.sectionY);
    if (section == nullptr)
    {
        return false; // Null section = all air
    }
    uint16_t blockId = section->getBlock(resolved.localX, resolved.localY, resolved.localZ);
    return registry.getBlockType(blockId).lightFilter == MAX_LIGHT_LEVEL;
}

/// Get block light at world position. Returns 0 if out of bounds or unloaded.
uint8_t getBlockLightAt(int worldX, int worldY, int worldZ, ChunkManager& manager)
{
    if (worldY < 0 || worldY >= ChunkColumn::COLUMN_HEIGHT)
    {
        return 0;
    }
    auto resolved = resolveWorldPos(worldX, worldY, worldZ, manager);
    if (resolved.column == nullptr)
    {
        return 0;
    }
    return resolved.column->getLightMap(resolved.sectionY).getBlockLight(resolved.localX, resolved.localY, resolved.localZ);
}

/// Set block light at world position. No-op if out of bounds or unloaded.
/// Returns true if the value was changed, marking the section dirty.
bool setBlockLightAt(int worldX, int worldY, int worldZ, uint8_t val, ChunkManager& manager)
{
    if (worldY < 0 || worldY >= ChunkColumn::COLUMN_HEIGHT)
    {
        return false;
    }
    auto resolved = resolveWorldPos(worldX, worldY, worldZ, manager);
    if (resolved.column == nullptr)
    {
        return false;
    }
    LightMap& lm = resolved.column->getLightMap(resolved.sectionY);
    if (lm.getBlockLight(resolved.localX, resolved.localY, resolved.localZ) == val)
    {
        return false;
    }
    lm.setBlockLight(resolved.localX, resolved.localY, resolved.localZ, val);
    resolved.column->markDirty(resolved.sectionY);
    return true;
}

/// Get sky light at world position. Returns 0 if out of bounds or unloaded.
uint8_t getSkyLightAt(int worldX, int worldY, int worldZ, ChunkManager& manager)
{
    if (worldY < 0 || worldY >= ChunkColumn::COLUMN_HEIGHT)
    {
        return (worldY >= ChunkColumn::COLUMN_HEIGHT) ? MAX_LIGHT_LEVEL : 0;
    }
    auto resolved = resolveWorldPos(worldX, worldY, worldZ, manager);
    if (resolved.column == nullptr)
    {
        return 0;
    }
    return resolved.column->getLightMap(resolved.sectionY).getSkyLight(resolved.localX, resolved.localY, resolved.localZ);
}

/// Set sky light at world position. No-op if out of bounds or unloaded.
/// Returns true if the value was changed, marking the section dirty.
bool setSkyLightAt(int worldX, int worldY, int worldZ, uint8_t val, ChunkManager& manager)
{
    if (worldY < 0 || worldY >= ChunkColumn::COLUMN_HEIGHT)
    {
        return false;
    }
    auto resolved = resolveWorldPos(worldX, worldY, worldZ, manager);
    if (resolved.column == nullptr)
    {
        return false;
    }
    LightMap& lm = resolved.column->getLightMap(resolved.sectionY);
    if (lm.getSkyLight(resolved.localX, resolved.localY, resolved.localZ) == val)
    {
        return false;
    }
    lm.setSkyLight(resolved.localX, resolved.localY, resolved.localZ, val);
    resolved.column->markDirty(resolved.sectionY);
    return true;
}

/// Convert column-local (localX, worldY, localZ) to world coordinates given a chunk column.
void toWorldCoords(const ChunkColumn& column, int localX, int worldY, int localZ, int& outX, int& outY, int& outZ)
{
    glm::ivec2 coord = column.getChunkCoord();
    outX = coord.x * ChunkSection::SIZE + localX;
    outY = worldY;
    outZ = coord.y * ChunkSection::SIZE + localZ;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// floodRemoveBlockLight — Reverse BFS for block light
// ─────────────────────────────────────────────────────────────────────────────

void DynamicLightUpdater::floodRemoveBlockLight(
    ChunkColumn& column,
    int localX,
    int worldY,
    int localZ,
    uint8_t startLight,
    ChunkManager& manager,
    const BlockRegistry& registry)
{
    int startWorldX = 0;
    int startWorldY = 0;
    int startWorldZ = 0;
    toWorldCoords(column, localX, worldY, localZ, startWorldX, startWorldY, startWorldZ);

    std::queue<LightRemovalNode> removeQueue;
    std::vector<LightRemovalNode> reseedQueue;

    removeQueue.push({static_cast<int16_t>(startWorldX), static_cast<int16_t>(startWorldY),
                      static_cast<int16_t>(startWorldZ), startLight});

    while (!removeQueue.empty())
    {
        LightRemovalNode node = removeQueue.front();
        removeQueue.pop();

        for (const auto& offset : OFFSETS)
        {
            int nx = node.x + offset[0];
            int ny = node.y + offset[1];
            int nz = node.z + offset[2];

            if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
            {
                continue;
            }

            uint8_t neighborLight = getBlockLightAt(nx, ny, nz, manager);
            if (neighborLight == 0)
            {
                continue;
            }

            if (neighborLight < node.light)
            {
                // This neighbor's light depended on the removed path
                setBlockLightAt(nx, ny, nz, 0, manager);
                removeQueue.push(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), neighborLight});
            }
            else
            {
                // This neighbor has an independent source — reseed from here
                reseedQueue.push_back(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), neighborLight});
            }
        }
    }

    // Phase 2: Re-propagate from boundary sources
    reseedBlockLight(reseedQueue, manager, registry);
}

// ─────────────────────────────────────────────────────────────────────────────
// floodRemoveSkyLight — Reverse BFS for sky light
// ─────────────────────────────────────────────────────────────────────────────

void DynamicLightUpdater::floodRemoveSkyLight(
    ChunkColumn& column,
    int localX,
    int worldY,
    int localZ,
    uint8_t startLight,
    ChunkManager& manager,
    const BlockRegistry& registry)
{
    int startWorldX = 0;
    int startWorldY = 0;
    int startWorldZ = 0;
    toWorldCoords(column, localX, worldY, localZ, startWorldX, startWorldY, startWorldZ);

    std::queue<LightRemovalNode> removeQueue;
    std::vector<LightRemovalNode> reseedQueue;

    removeQueue.push({static_cast<int16_t>(startWorldX), static_cast<int16_t>(startWorldY),
                      static_cast<int16_t>(startWorldZ), startLight});

    while (!removeQueue.empty())
    {
        LightRemovalNode node = removeQueue.front();
        removeQueue.pop();

        for (const auto& offset : OFFSETS)
        {
            int nx = node.x + offset[0];
            int ny = node.y + offset[1];
            int nz = node.z + offset[2];

            if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
            {
                continue;
            }

            uint8_t neighborLight = getSkyLightAt(nx, ny, nz, manager);
            if (neighborLight == 0)
            {
                continue;
            }

            // For sky light, downward propagation is special: light=15 going down stays 15.
            // A neighbor below with the same level was probably from downward propagation.
            bool isDownward = (offset[1] == -1);
            bool shouldRemove = false;

            if (isDownward && node.light == MAX_LIGHT_LEVEL && neighborLight == MAX_LIGHT_LEVEL)
            {
                // Downward sky propagation: this neighbor inherited full sky from above
                shouldRemove = true;
            }
            else if (neighborLight < node.light)
            {
                shouldRemove = true;
            }

            if (shouldRemove)
            {
                setSkyLightAt(nx, ny, nz, 0, manager);
                removeQueue.push(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), neighborLight});
            }
            else
            {
                reseedQueue.push_back(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), neighborLight});
            }
        }
    }

    // Phase 2: Re-propagate from boundary sources
    reseedSkyLight(reseedQueue, manager, registry);
}

// ─────────────────────────────────────────────────────────────────────────────
// reseedBlockLight — BFS re-propagation from remaining block light sources
// ─────────────────────────────────────────────────────────────────────────────

void DynamicLightUpdater::reseedBlockLight(
    std::vector<LightRemovalNode>& seeds,
    ChunkManager& manager,
    const BlockRegistry& registry)
{
    std::queue<LightRemovalNode> queue;
    for (const auto& seed : seeds)
    {
        queue.push(seed);
    }

    while (!queue.empty())
    {
        LightRemovalNode node = queue.front();
        queue.pop();

        for (const auto& offset : OFFSETS)
        {
            int nx = node.x + offset[0];
            int ny = node.y + offset[1];
            int nz = node.z + offset[2];

            if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
            {
                continue;
            }

            if (isBlockOpaque(nx, ny, nz, manager, registry))
            {
                continue;
            }

            uint8_t newLight = node.light - 1;
            if (newLight == 0)
            {
                continue;
            }

            uint8_t existing = getBlockLightAt(nx, ny, nz, manager);
            if (newLight > existing)
            {
                setBlockLightAt(nx, ny, nz, newLight, manager);
                queue.push(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), newLight});
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// reseedSkyLight — BFS re-propagation from remaining sky light sources
// ─────────────────────────────────────────────────────────────────────────────

void DynamicLightUpdater::reseedSkyLight(
    std::vector<LightRemovalNode>& seeds,
    ChunkManager& manager,
    const BlockRegistry& registry)
{
    std::queue<LightRemovalNode> queue;
    for (const auto& seed : seeds)
    {
        queue.push(seed);
    }

    while (!queue.empty())
    {
        LightRemovalNode node = queue.front();
        queue.pop();

        for (const auto& offset : OFFSETS)
        {
            int nx = node.x + offset[0];
            int ny = node.y + offset[1];
            int nz = node.z + offset[2];

            if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
            {
                continue;
            }

            if (isBlockOpaque(nx, ny, nz, manager, registry))
            {
                continue;
            }

            // Sky light rule: downward = no attenuation, other = -1
            bool isDownward = (offset[1] == -1);
            uint8_t newLight = isDownward ? node.light : static_cast<uint8_t>(node.light - 1);
            if (newLight == 0)
            {
                continue;
            }

            uint8_t existing = getSkyLightAt(nx, ny, nz, manager);
            if (newLight > existing)
            {
                setSkyLightAt(nx, ny, nz, newLight, manager);
                queue.push(
                    {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), newLight});
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onBlockBroken — main entry point for block removal
// ─────────────────────────────────────────────────────────────────────────────

void DynamicLightUpdater::onBlockBroken(
    ChunkColumn& column,
    int localX,
    int worldY,
    int localZ,
    const BlockDefinition& oldBlock,
    ChunkManager& manager,
    const BlockRegistry& registry)
{
    int sectionY = worldY / ChunkSection::SIZE;
    int localY = worldY % ChunkSection::SIZE;

    int worldX = 0;
    int tmpY = 0;
    int worldZ = 0;
    toWorldCoords(column, localX, worldY, localZ, worldX, tmpY, worldZ);

    // 1. If removed block emitted light, reverse-BFS remove its contribution
    if (oldBlock.lightEmission > 0)
    {
        // Light at this position was already cleared by setBlock (block is now air with light=0).
        // But we need to seed the removal BFS. The light at this position was the emission value.
        column.getLightMap(sectionY).setBlockLight(localX, localY, localZ, 0);
        floodRemoveBlockLight(column, localX, worldY, localZ, oldBlock.lightEmission, manager, registry);
    }

    // 2. If removed block was opaque, light from neighbors can now flow through
    if (oldBlock.lightFilter == MAX_LIGHT_LEVEL)
    {
        // Re-propagate block light from neighboring sources into the gap
        std::vector<LightRemovalNode> blockSeeds;
        for (const auto& offset : OFFSETS)
        {
            int nx = worldX + offset[0];
            int ny = worldY + offset[1];
            int nz = worldZ + offset[2];

            if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
            {
                continue;
            }

            uint8_t neighborBlockLight = getBlockLightAt(nx, ny, nz, manager);
            if (neighborBlockLight > 1)
            {
                blockSeeds.push_back({static_cast<int16_t>(nx), static_cast<int16_t>(ny),
                                      static_cast<int16_t>(nz), neighborBlockLight});
            }
        }
        if (!blockSeeds.empty())
        {
            reseedBlockLight(blockSeeds, manager, registry);
        }

        // 3. Handle sky light: if this block was at or above heightmap, rebuild heightmap column
        uint8_t oldHeight = column.getHeight(localX, localZ);
        if (worldY >= static_cast<int>(oldHeight))
        {
            // Rescan this (x,z) column to find new highest opaque block
            uint8_t newHeight = 0;
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
            {
                int sy = y / ChunkSection::SIZE;
                int ly = y % ChunkSection::SIZE;
                const ChunkSection* sec = column.getSection(sy);
                if (sec != nullptr)
                {
                    uint16_t blockId = sec->getBlock(localX, ly, localZ);
                    if (registry.getBlockType(blockId).lightFilter == MAX_LIGHT_LEVEL)
                    {
                        newHeight = static_cast<uint8_t>(y);
                        break;
                    }
                }
            }
            column.setHeight(localX, localZ, newHeight);

            // Seed sky light from above the new heightmap down through the newly exposed column
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y > static_cast<int>(newHeight); --y)
            {
                if (!isBlockOpaque(worldX, y, worldZ, manager, registry))
                {
                    setSkyLightAt(worldX, y, worldZ, MAX_LIGHT_LEVEL, manager);
                }
            }

            // BFS propagate sky light horizontally from the newly lit column
            std::vector<LightRemovalNode> skySeeds;
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y > static_cast<int>(newHeight); --y)
            {
                if (getSkyLightAt(worldX, y, worldZ, manager) == MAX_LIGHT_LEVEL)
                {
                    skySeeds.push_back(
                        {static_cast<int16_t>(worldX), static_cast<int16_t>(y), static_cast<int16_t>(worldZ),
                         MAX_LIGHT_LEVEL});
                }
            }
            if (!skySeeds.empty())
            {
                reseedSkyLight(skySeeds, manager, registry);
            }
        }
        else
        {
            // Block was below heightmap — just re-propagate sky light from neighbors into the gap
            std::vector<LightRemovalNode> skySeeds;
            for (const auto& offset : OFFSETS)
            {
                int nx = worldX + offset[0];
                int ny = worldY + offset[1];
                int nz = worldZ + offset[2];

                if (ny < 0 || ny >= ChunkColumn::COLUMN_HEIGHT)
                {
                    continue;
                }

                uint8_t neighborSkyLight = getSkyLightAt(nx, ny, nz, manager);
                if (neighborSkyLight > 0)
                {
                    skySeeds.push_back({static_cast<int16_t>(nx), static_cast<int16_t>(ny),
                                        static_cast<int16_t>(nz), neighborSkyLight});
                }
            }
            if (!skySeeds.empty())
            {
                reseedSkyLight(skySeeds, manager, registry);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onBlockPlaced — main entry point for block placement
// ─────────────────────────────────────────────────────────────────────────────

void DynamicLightUpdater::onBlockPlaced(
    ChunkColumn& column,
    int localX,
    int worldY,
    int localZ,
    const BlockDefinition& newBlock,
    ChunkManager& manager,
    const BlockRegistry& registry)
{
    int sectionY = worldY / ChunkSection::SIZE;
    int localY = worldY % ChunkSection::SIZE;

    int worldX = 0;
    int tmpY = 0;
    int worldZ = 0;
    toWorldCoords(column, localX, worldY, localZ, worldX, tmpY, worldZ);

    // 1. If new block emits light, seed BFS from it
    if (newBlock.lightEmission > 0)
    {
        column.getLightMap(sectionY).setBlockLight(localX, localY, localZ, newBlock.lightEmission);
        column.markDirty(sectionY);

        std::vector<LightRemovalNode> seeds;
        seeds.push_back({static_cast<int16_t>(worldX), static_cast<int16_t>(worldY),
                         static_cast<int16_t>(worldZ), newBlock.lightEmission});
        reseedBlockLight(seeds, manager, registry);
    }

    // 2. If new block is opaque, remove light passing through this position
    if (newBlock.lightFilter == MAX_LIGHT_LEVEL)
    {
        LightMap& lm = column.getLightMap(sectionY);
        uint8_t existingBlockLight = lm.getBlockLight(localX, localY, localZ);
        uint8_t existingSkyLight = lm.getSkyLight(localX, localY, localZ);

        if (existingBlockLight > 0)
        {
            lm.setBlockLight(localX, localY, localZ, 0);
            column.markDirty(sectionY);
            floodRemoveBlockLight(column, localX, worldY, localZ, existingBlockLight, manager, registry);
        }

        if (existingSkyLight > 0)
        {
            lm.setSkyLight(localX, localY, localZ, 0);
            column.markDirty(sectionY);
            floodRemoveSkyLight(column, localX, worldY, localZ, existingSkyLight, manager, registry);
        }

        // 3. Update heightmap
        uint8_t currentHeight = column.getHeight(localX, localZ);
        if (worldY > static_cast<int>(currentHeight))
        {
            column.setHeight(localX, localZ, static_cast<uint8_t>(worldY));
        }
    }
}

} // namespace voxel::world
