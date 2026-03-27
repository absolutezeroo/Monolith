#include "voxel/world/StructureGenerator.h"

#include "voxel/core/Log.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace voxel::world
{

// ── Deterministic hash for per-chunk RNG seeding ─────────────────────────────

static uint32_t chunkHash(int64_t seed, int64_t offset, int cx, int cz)
{
    int64_t h = seed + offset + static_cast<int64_t>(cx) * 341873128712LL + static_cast<int64_t>(cz) * 132897987541LL;
    return static_cast<uint32_t>(h & 0xFFFFFFFF);
}

// ── Block ID resolution helper ───────────────────────────────────────────────

static uint16_t resolveBlock(const BlockRegistry& registry, std::string_view name)
{
    uint16_t id = registry.getIdByName(name);
    if (id == BLOCK_AIR)
    {
        VX_LOG_WARN("StructureGenerator: block '{}' not registered, falling back to BLOCK_AIR", name);
    }
    return id;
}

// ── Constructor ──────────────────────────────────────────────────────────────

StructureGenerator::StructureGenerator(uint64_t seed, const BlockRegistry& registry)
    : m_seed(seed)
    , m_oakLogId(resolveBlock(registry, "base:oak_log"))
    , m_oakLeavesId(resolveBlock(registry, "base:oak_leaves"))
    , m_birchLogId(resolveBlock(registry, "base:birch_log"))
    , m_birchLeavesId(resolveBlock(registry, "base:birch_leaves"))
    , m_spruceLogId(resolveBlock(registry, "base:spruce_log"))
    , m_spruceLeavesId(resolveBlock(registry, "base:spruce_leaves"))
    , m_jungleLogId(resolveBlock(registry, "base:jungle_log"))
    , m_jungleLeavesId(resolveBlock(registry, "base:jungle_leaves"))
    , m_cactusId(resolveBlock(registry, "base:cactus"))
    , m_tallGrassId(resolveBlock(registry, "base:tall_grass"))
    , m_flowerRedId(resolveBlock(registry, "base:flower_red"))
    , m_flowerYellowId(resolveBlock(registry, "base:flower_yellow"))
    , m_deadBushId(resolveBlock(registry, "base:dead_bush"))
    , m_snowLayerId(resolveBlock(registry, "base:snow_layer"))
    , m_stoneId(resolveBlock(registry, "base:stone"))
    , m_sandId(resolveBlock(registry, "base:sand"))
    , m_snowBlockId(resolveBlock(registry, "base:snow_block"))
    , m_grassBlockId(resolveBlock(registry, "base:grass_block"))
    , m_dirtId(resolveBlock(registry, "base:dirt"))
{
    m_coalOre = {resolveBlock(registry, "base:coal_ore"), 20, 5, 128, 4, 12};
    m_ironOre = {resolveBlock(registry, "base:iron_ore"), 12, 5, 64, 3, 8};
    m_goldOre = {resolveBlock(registry, "base:gold_ore"), 4, 5, 32, 3, 6};
    m_diamondOre = {resolveBlock(registry, "base:diamond_ore"), 1, 5, 16, 2, 4};

    // Check if tree blocks are available (at least one log block registered)
    m_hasTreeBlocks = (m_oakLogId != BLOCK_AIR || m_birchLogId != BLOCK_AIR || m_spruceLogId != BLOCK_AIR ||
                       m_jungleLogId != BLOCK_AIR || m_cactusId != BLOCK_AIR);

    m_hasDecorationBlocks = (m_tallGrassId != BLOCK_AIR || m_flowerRedId != BLOCK_AIR ||
                             m_flowerYellowId != BLOCK_AIR || m_deadBushId != BLOCK_AIR ||
                             m_snowLayerId != BLOCK_AIR);
}

// ── Public entry points ──────────────────────────────────────────────────────

void StructureGenerator::populateOres(ChunkColumn& column, glm::ivec2 chunkCoord) const
{
    placeOres(column, chunkCoord);
}

void StructureGenerator::populateStructures(
    ChunkColumn& column,
    glm::ivec2 chunkCoord,
    const BiomeSystem& biomeSystem,
    const int surfaceHeights[16][16],
    int (*computeSurfaceHeightFn)(float worldX, float worldZ, const void* userData),
    const void* userData) const
{
    placeTrees(column, chunkCoord, biomeSystem, surfaceHeights, computeSurfaceHeightFn, userData);
    placeDecorations(column, chunkCoord, biomeSystem, surfaceHeights);
}

// ── Ore vein generation ──────────────────────────────────────────────────────

void StructureGenerator::placeOres(ChunkColumn& column, glm::ivec2 chunkCoord) const
{
    std::mt19937 rng(chunkHash(static_cast<int64_t>(m_seed), ORE_SEED_OFFSET, chunkCoord.x, chunkCoord.y));

    const OreConfig* ores[] = {&m_coalOre, &m_ironOre, &m_goldOre, &m_diamondOre};

    for (const OreConfig* ore : ores)
    {
        if (ore->blockId == BLOCK_AIR)
        {
            continue;
        }

        for (int v = 0; v < ore->veinsPerChunk; ++v)
        {
            std::uniform_int_distribution<int> xzDist(0, ChunkSection::SIZE - 1);
            std::uniform_int_distribution<int> yDist(ore->minY, ore->maxY);
            std::uniform_int_distribution<int> sizeDist(ore->minVeinSize, ore->maxVeinSize);

            int startX = xzDist(rng);
            int startZ = xzDist(rng);
            int startY = yDist(rng);
            int veinSize = sizeDist(rng);

            // BFS-walk from start position
            int cx = startX;
            int cy = startY;
            int cz = startZ;

            for (int i = 0; i < veinSize; ++i)
            {
                if (cx >= 0 && cx < ChunkSection::SIZE && cz >= 0 && cz < ChunkSection::SIZE && cy >= 0 &&
                    cy < ChunkColumn::COLUMN_HEIGHT)
                {
                    if (column.getBlock(cx, cy, cz) == m_stoneId)
                    {
                        column.setBlock(cx, cy, cz, ore->blockId);
                    }
                }

                // Random walk to adjacent position
                std::uniform_int_distribution<int> dirDist(0, 5);
                int dir = dirDist(rng);
                switch (dir)
                {
                case 0:
                    ++cx;
                    break;
                case 1:
                    --cx;
                    break;
                case 2:
                    ++cy;
                    break;
                case 3:
                    --cy;
                    break;
                case 4:
                    ++cz;
                    break;
                case 5:
                    --cz;
                    break;
                }
            }
        }
    }
}

// ── Tree schematic builders ──────────────────────────────────────────────────

TreeSchematic StructureGenerator::buildOak(std::mt19937& rng) const
{
    TreeSchematic tree;
    tree.trunkBlock = m_oakLogId;
    tree.leafBlock = m_oakLeavesId;

    std::uniform_int_distribution<int> heightDist(4, 6);
    int height = heightDist(rng);

    // Trunk
    for (int y = 0; y < height; ++y)
    {
        tree.trunkOffsets.push_back({0, y, 0});
    }

    // Leaves: 5x5x3 canopy starting 1 below trunk top, corners removed
    int leafBaseY = height - 2;
    for (int dy = 0; dy < 3; ++dy)
    {
        int y = leafBaseY + dy;
        int radius = (dy == 2) ? 1 : 2; // top layer narrower
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                // Skip corners on the wider layers
                if (radius == 2 && std::abs(dx) == 2 && std::abs(dz) == 2)
                {
                    continue;
                }
                // Skip trunk position
                if (dx == 0 && dz == 0 && y < height)
                {
                    continue;
                }
                tree.leafOffsets.push_back({dx, y, dz});
            }
        }
    }
    // Top leaf
    tree.leafOffsets.push_back({0, height, 0});

    return tree;
}

TreeSchematic StructureGenerator::buildBirch(std::mt19937& rng) const
{
    TreeSchematic tree;
    tree.trunkBlock = m_birchLogId;
    tree.leafBlock = m_birchLeavesId;

    std::uniform_int_distribution<int> heightDist(5, 7);
    int height = heightDist(rng);

    for (int y = 0; y < height; ++y)
    {
        tree.trunkOffsets.push_back({0, y, 0});
    }

    // Leaves: 3x3x3 canopy at trunk top
    int leafBaseY = height - 2;
    for (int dy = 0; dy < 3; ++dy)
    {
        int y = leafBaseY + dy;
        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dz = -1; dz <= 1; ++dz)
            {
                if (dx == 0 && dz == 0 && y < height)
                {
                    continue;
                }
                tree.leafOffsets.push_back({dx, y, dz});
            }
        }
    }
    tree.leafOffsets.push_back({0, height, 0});

    return tree;
}

TreeSchematic StructureGenerator::buildSpruce(std::mt19937& rng) const
{
    TreeSchematic tree;
    tree.trunkBlock = m_spruceLogId;
    tree.leafBlock = m_spruceLeavesId;

    std::uniform_int_distribution<int> heightDist(6, 8);
    int height = heightDist(rng);

    for (int y = 0; y < height; ++y)
    {
        tree.trunkOffsets.push_back({0, y, 0});
    }

    // Cone-shaped leaves: widest at bottom, narrowing to 1x1 at top
    // Bottom layer (5x5) at height-4, middle layers (3x3), top (1x1)
    int leafStartY = height - 4;
    if (leafStartY < 2)
    {
        leafStartY = 2;
    }

    int totalLeafLayers = height - leafStartY + 1;
    for (int layer = 0; layer < totalLeafLayers; ++layer)
    {
        int y = leafStartY + layer;
        int radius;
        float progress = static_cast<float>(layer) / static_cast<float>(std::max(1, totalLeafLayers - 1));
        if (progress < 0.33f)
        {
            radius = 2; // 5x5
        }
        else if (progress < 0.75f)
        {
            radius = 1; // 3x3
        }
        else
        {
            radius = 0; // 1x1
        }

        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                if (dx == 0 && dz == 0 && y < height)
                {
                    continue;
                }
                tree.leafOffsets.push_back({dx, y, dz});
            }
        }
    }
    // Top leaf
    tree.leafOffsets.push_back({0, height, 0});

    return tree;
}

TreeSchematic StructureGenerator::buildJungle(std::mt19937& rng) const
{
    TreeSchematic tree;
    tree.trunkBlock = m_jungleLogId;
    tree.leafBlock = m_jungleLeavesId;

    std::uniform_int_distribution<int> heightDist(8, 12);
    int height = heightDist(rng);

    for (int y = 0; y < height; ++y)
    {
        tree.trunkOffsets.push_back({0, y, 0});
    }

    // Leaves: 7x7x4 large canopy, corners removed
    int leafBaseY = height - 2;
    for (int dy = 0; dy < 4; ++dy)
    {
        int y = leafBaseY + dy;
        int radius = (dy >= 3) ? 1 : 3; // top layer narrower
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                // Skip corners on wider layers
                if (radius == 3 && std::abs(dx) == 3 && std::abs(dz) == 3)
                {
                    continue;
                }
                if (dx == 0 && dz == 0 && y < height)
                {
                    continue;
                }
                tree.leafOffsets.push_back({dx, y, dz});
            }
        }
    }
    tree.leafOffsets.push_back({0, height + 1, 0});

    return tree;
}

TreeSchematic StructureGenerator::buildCactus(std::mt19937& rng) const
{
    TreeSchematic tree;
    tree.trunkBlock = m_cactusId;
    tree.leafBlock = BLOCK_AIR; // No leaves

    std::uniform_int_distribution<int> heightDist(1, 3);
    int height = heightDist(rng);

    for (int y = 0; y < height; ++y)
    {
        tree.trunkOffsets.push_back({0, y, 0});
    }

    return tree;
}

// ── Tree type selection ──────────────────────────────────────────────────────

float StructureGenerator::getTreeDensity(BiomeType biome) const
{
    switch (biome)
    {
    case BiomeType::Desert:
        return 0.001f;
    case BiomeType::Plains:
        return 0.005f;
    case BiomeType::Savanna:
        return 0.003f;
    case BiomeType::Forest:
        return 0.04f;
    case BiomeType::Jungle:
        return 0.06f;
    case BiomeType::Taiga:
        return 0.03f;
    case BiomeType::Tundra:
        return 0.0f;
    case BiomeType::IcePlains:
        return 0.0f;
    default:
        return 0.0f;
    }
}

TreeSchematic StructureGenerator::selectTree(BiomeType biome, std::mt19937& rng) const
{
    switch (biome)
    {
    case BiomeType::Desert:
        return buildCactus(rng);
    case BiomeType::Taiga:
        return buildSpruce(rng);
    case BiomeType::Jungle:
        return buildJungle(rng);
    case BiomeType::Forest: {
        // Forest: mix of oak and birch
        std::uniform_int_distribution<int> dist(0, 2);
        int roll = dist(rng);
        if (roll == 0)
        {
            return buildBirch(rng);
        }
        return buildOak(rng);
    }
    case BiomeType::Plains:
        return buildOak(rng);
    case BiomeType::Savanna:
        return buildOak(rng);
    default:
        return buildOak(rng);
    }
}

// ── Tree placement with 3x3 cross-chunk overlap ─────────────────────────────

void StructureGenerator::placeTrees(
    ChunkColumn& column,
    glm::ivec2 chunkCoord,
    const BiomeSystem& biomeSystem,
    const int surfaceHeights[16][16],
    int (*computeSurfaceHeightFn)(float worldX, float worldZ, const void* userData),
    const void* userData) const
{
    if (!m_hasTreeBlocks)
    {
        return;
    }

    // Spacing grid: 3 chunks wide (48x48)
    static constexpr int GRID_SIZE = 48;
    bool occupied[GRID_SIZE][GRID_SIZE] = {};

    for (int nx = chunkCoord.x - 1; nx <= chunkCoord.x + 1; ++nx)
    {
        for (int nz = chunkCoord.y - 1; nz <= chunkCoord.y + 1; ++nz)
        {
            std::mt19937 rng(
                chunkHash(static_cast<int64_t>(m_seed), TREE_SEED_OFFSET, nx, nz));

            for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
            {
                for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
                {
                    int worldX = nx * ChunkSection::SIZE + lx;
                    int worldZ = nz * ChunkSection::SIZE + lz;

                    BiomeType biome = biomeSystem.getBiomeAt(
                        static_cast<float>(worldX), static_cast<float>(worldZ));
                    float density = getTreeDensity(biome);

                    // Roll RNG — must always be consumed for determinism
                    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                    float roll = chanceDist(rng);

                    if (roll >= density)
                    {
                        continue;
                    }

                    // Spacing check on the 48x48 grid
                    int gridX = (nx - chunkCoord.x + 1) * ChunkSection::SIZE + lx;
                    int gridZ = (nz - chunkCoord.y + 1) * ChunkSection::SIZE + lz;

                    bool tooClose = false;
                    int minGX = std::max(0, gridX - TREE_SPACING);
                    int maxGX = std::min(GRID_SIZE - 1, gridX + TREE_SPACING);
                    int minGZ = std::max(0, gridZ - TREE_SPACING);
                    int maxGZ = std::min(GRID_SIZE - 1, gridZ + TREE_SPACING);
                    for (int gx = minGX; gx <= maxGX && !tooClose; ++gx)
                    {
                        for (int gz = minGZ; gz <= maxGZ && !tooClose; ++gz)
                        {
                            if (occupied[gx][gz])
                            {
                                tooClose = true;
                            }
                        }
                    }
                    if (tooClose)
                    {
                        continue;
                    }

                    occupied[gridX][gridZ] = true;

                    // Compute surface height
                    int surfaceH;
                    int localToTargetX = worldX - chunkCoord.x * ChunkSection::SIZE;
                    int localToTargetZ = worldZ - chunkCoord.y * ChunkSection::SIZE;
                    bool isInTargetChunk = (localToTargetX >= 0 && localToTargetX < ChunkSection::SIZE &&
                                            localToTargetZ >= 0 && localToTargetZ < ChunkSection::SIZE);

                    if (isInTargetChunk)
                    {
                        surfaceH = surfaceHeights[localToTargetX][localToTargetZ];
                    }
                    else
                    {
                        surfaceH = computeSurfaceHeightFn(
                            static_cast<float>(worldX), static_cast<float>(worldZ), userData);
                    }

                    // Surface validation: root must be on solid surface
                    if (surfaceH <= 0 || surfaceH >= ChunkColumn::COLUMN_HEIGHT - 1)
                    {
                        continue;
                    }

                    // Cactus only on sand
                    if (biome == BiomeType::Desert)
                    {
                        if (isInTargetChunk && column.getBlock(localToTargetX, surfaceH, localToTargetZ) != m_sandId)
                        {
                            continue;
                        }
                    }

                    // Build tree schematic
                    TreeSchematic tree = selectTree(biome, rng);

                    // Place tree blocks that fall within target chunk
                    for (const auto& offset : tree.trunkOffsets)
                    {
                        int bx = localToTargetX + offset.x;
                        int by = surfaceH + 1 + offset.y;
                        int bz = localToTargetZ + offset.z;

                        if (bx >= 0 && bx < ChunkSection::SIZE && bz >= 0 && bz < ChunkSection::SIZE && by >= 0 &&
                            by < ChunkColumn::COLUMN_HEIGHT)
                        {
                            uint16_t existing = column.getBlock(bx, by, bz);
                            // Trunks overwrite air and leaves
                            if (existing == BLOCK_AIR || existing == m_oakLeavesId || existing == m_birchLeavesId ||
                                existing == m_spruceLeavesId || existing == m_jungleLeavesId)
                            {
                                column.setBlock(bx, by, bz, tree.trunkBlock);
                            }
                        }
                    }

                    for (const auto& offset : tree.leafOffsets)
                    {
                        int bx = localToTargetX + offset.x;
                        int by = surfaceH + 1 + offset.y;
                        int bz = localToTargetZ + offset.z;

                        if (bx >= 0 && bx < ChunkSection::SIZE && bz >= 0 && bz < ChunkSection::SIZE && by >= 0 &&
                            by < ChunkColumn::COLUMN_HEIGHT)
                        {
                            // Leaves only overwrite air
                            if (column.getBlock(bx, by, bz) == BLOCK_AIR)
                            {
                                column.setBlock(bx, by, bz, tree.leafBlock);
                            }
                        }
                    }
                }
            }
        }
    }
}

// ── Surface decoration placement ─────────────────────────────────────────────

void StructureGenerator::placeDecorations(
    ChunkColumn& column,
    glm::ivec2 chunkCoord,
    const BiomeSystem& biomeSystem,
    const int surfaceHeights[16][16]) const
{
    if (!m_hasDecorationBlocks)
    {
        return;
    }

    // Decoration RNG uses tree seed offset + 1000 to not interfere with tree RNG
    std::mt19937 rng(chunkHash(static_cast<int64_t>(m_seed), TREE_SEED_OFFSET + 1000, chunkCoord.x, chunkCoord.y));
    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);

    for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
    {
        for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
        {
            int worldX = chunkCoord.x * ChunkSection::SIZE + lx;
            int worldZ = chunkCoord.y * ChunkSection::SIZE + lz;

            BiomeType biome = biomeSystem.getBiomeAt(static_cast<float>(worldX), static_cast<float>(worldZ));
            int surfaceH = surfaceHeights[lx][lz];
            int decorY = surfaceH + 1;

            if (decorY <= 0 || decorY >= ChunkColumn::COLUMN_HEIGHT)
            {
                continue;
            }

            // Only place on air
            if (column.getBlock(lx, decorY, lz) != BLOCK_AIR)
            {
                continue;
            }

            // Get surface block for validation
            uint16_t surfaceBlock = column.getBlock(lx, surfaceH, lz);

            float roll = chanceDist(rng);
            uint16_t decorBlock = BLOCK_AIR;

            switch (biome)
            {
            case BiomeType::Plains:
                if (roll < 0.30f)
                {
                    decorBlock = m_tallGrassId;
                }
                else if (roll < 0.33f)
                {
                    decorBlock = m_flowerRedId;
                }
                else if (roll < 0.35f)
                {
                    decorBlock = m_flowerYellowId;
                }
                break;

            case BiomeType::Forest:
                if (roll < 0.40f)
                {
                    decorBlock = m_tallGrassId;
                }
                else if (roll < 0.45f)
                {
                    decorBlock = m_flowerRedId;
                }
                else if (roll < 0.48f)
                {
                    decorBlock = m_flowerYellowId;
                }
                break;

            case BiomeType::Savanna:
                if (roll < 0.15f)
                {
                    decorBlock = m_tallGrassId;
                }
                else if (roll < 0.16f)
                {
                    decorBlock = m_deadBushId;
                }
                break;

            case BiomeType::Desert:
                if (roll < 0.05f)
                {
                    decorBlock = m_deadBushId;
                }
                else if (roll < 0.07f)
                {
                    decorBlock = m_cactusId;
                }
                break;

            case BiomeType::Jungle:
                if (roll < 0.50f)
                {
                    decorBlock = m_tallGrassId;
                }
                else if (roll < 0.52f)
                {
                    decorBlock = m_flowerRedId;
                }
                break;

            case BiomeType::Taiga:
                if (roll < 0.10f)
                {
                    decorBlock = m_tallGrassId;
                }
                break;

            case BiomeType::Tundra:
                if (roll < 0.80f)
                {
                    decorBlock = m_snowLayerId;
                }
                break;

            case BiomeType::IcePlains:
                if (roll < 0.90f)
                {
                    decorBlock = m_snowLayerId;
                }
                break;

            default:
                break;
            }

            if (decorBlock == BLOCK_AIR)
            {
                continue;
            }

            // Surface validation: don't place grass/flowers on sand, dead_bush on snow, cactus only on sand
            bool isGrassOrFlower = (decorBlock == m_tallGrassId || decorBlock == m_flowerRedId ||
                                    decorBlock == m_flowerYellowId);
            if (isGrassOrFlower && (surfaceBlock == m_sandId || surfaceBlock == m_snowBlockId))
            {
                continue;
            }
            if (decorBlock == m_deadBushId && surfaceBlock == m_snowBlockId)
            {
                continue;
            }
            if (decorBlock == m_cactusId && surfaceBlock != m_sandId)
            {
                continue;
            }

            column.setBlock(lx, decorY, lz, decorBlock);
        }
    }
}

} // namespace voxel::world
