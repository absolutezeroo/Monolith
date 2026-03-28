#include "voxel/world/ChunkManager.h"

#include "voxel/core/Assert.h"
#include "voxel/core/JobSystem.h"
#include "voxel/core/Log.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/world/Block.h"
#include "voxel/world/WorldGenerator.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace voxel::world
{

ChunkColumn* ChunkManager::getChunk(glm::ivec2 coord)
{
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

const ChunkColumn* ChunkManager::getChunk(glm::ivec2 coord) const
{
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

uint16_t ChunkManager::getBlock(const glm::ivec3& worldPos) const
{
    glm::ivec2 chunkCoord = worldToChunkCoord(worldPos);
    const ChunkColumn* column = getChunk(chunkCoord);
    if (column == nullptr)
    {
        return BLOCK_AIR;
    }

    glm::ivec3 local = worldToLocalPos(worldPos);
    return column->getBlock(local.x, local.y, local.z);
}

void ChunkManager::setBlock(const glm::ivec3& worldPos, uint16_t id)
{
    glm::ivec2 chunkCoord = worldToChunkCoord(worldPos);
    ChunkColumn* column = getChunk(chunkCoord);

    VX_ASSERT(column != nullptr, "setBlock called on unloaded chunk — load chunk first");
    if (column == nullptr)
    {
        return;
    }

    glm::ivec3 local = worldToLocalPos(worldPos);
    column->setBlock(local.x, local.y, local.z, id);

    // Neighbor dirty invalidation (AC8): when a block is on a section boundary,
    // the adjacent neighbor section also needs remeshing for correct face culling and AO.
    int sectionY = worldPos.y / ChunkSection::SIZE;

    // X boundaries
    if (local.x == 0)
    {
        ChunkColumn* neighbor = getChunk(chunkCoord + glm::ivec2{-1, 0});
        if (neighbor != nullptr)
        {
            neighbor->markDirty(sectionY);
        }
    }
    if (local.x == ChunkSection::SIZE - 1)
    {
        ChunkColumn* neighbor = getChunk(chunkCoord + glm::ivec2{1, 0});
        if (neighbor != nullptr)
        {
            neighbor->markDirty(sectionY);
        }
    }

    // Z boundaries
    if (local.z == 0)
    {
        ChunkColumn* neighbor = getChunk(chunkCoord + glm::ivec2{0, -1});
        if (neighbor != nullptr)
        {
            neighbor->markDirty(sectionY);
        }
    }
    if (local.z == ChunkSection::SIZE - 1)
    {
        ChunkColumn* neighbor = getChunk(chunkCoord + glm::ivec2{0, 1});
        if (neighbor != nullptr)
        {
            neighbor->markDirty(sectionY);
        }
    }

    // Y boundaries (same column, different section)
    int localY = worldPos.y % ChunkSection::SIZE;
    if (localY == 0 && sectionY > 0)
    {
        column->markDirty(sectionY - 1);
    }
    if (localY == ChunkSection::SIZE - 1 && sectionY < ChunkColumn::SECTIONS_PER_COLUMN - 1)
    {
        column->markDirty(sectionY + 1);
    }
}

void ChunkManager::loadChunk(glm::ivec2 coord)
{
    auto [it, inserted] = m_chunks.try_emplace(coord, nullptr);
    if (inserted)
    {
        if (m_worldGen != nullptr)
        {
            it->second = std::make_unique<ChunkColumn>(m_worldGen->generateChunkColumn(coord));
        }
        else
        {
            it->second = std::make_unique<ChunkColumn>(coord);
        }
    }
}

void ChunkManager::unloadChunk(glm::ivec2 coord)
{
    m_chunks.erase(coord);

    // Remove any completed meshes for this chunk
    for (int s = 0; s < ChunkColumn::SECTIONS_PER_COLUMN; ++s)
    {
        MeshKey key{coord, s};
        m_meshes.erase(key);
        // Note: in-flight tasks for this chunk will be discarded when results arrive (AC10)
    }
}

size_t ChunkManager::loadedChunkCount() const
{
    return m_chunks.size();
}

size_t ChunkManager::dirtyChunkCount() const
{
    size_t count = 0;
    for (const auto& [coord, column] : m_chunks)
    {
        for (int s = 0; s < ChunkColumn::SECTIONS_PER_COLUMN; ++s)
        {
            if (column->isSectionDirty(s))
            {
                ++count;
                break;
            }
        }
    }
    return count;
}

void ChunkManager::shutdown()
{
    if (m_jobSystem == nullptr)
    {
        return;
    }

    // Wait for every in-flight task so worker threads no longer reference
    // MeshBuilder or the ConcurrentQueue owned by this ChunkManager.
    for (auto& task : m_inFlightTasks)
    {
        m_jobSystem->getScheduler().WaitforTask(task.get());
    }
    m_inFlightTasks.clear();
    m_inFlightKeys.clear();
}

void ChunkManager::update(const glm::dvec3& playerPos)
{
    if (m_jobSystem == nullptr || m_meshBuilder == nullptr)
    {
        return;
    }

    streamChunks(playerPos);
    cleanupCompletedTasks();
    pollMeshResults();
    dispatchDirtySections(playerPos);
}

void ChunkManager::streamChunks(const glm::dvec3& playerPos)
{
    glm::ivec2 playerChunk = worldToChunkCoord(glm::ivec3(
        static_cast<int>(std::floor(playerPos.x)),
        static_cast<int>(std::floor(playerPos.y)),
        static_cast<int>(std::floor(playerPos.z))));

    int rd = m_renderDistance;

    // Load new chunks within render distance (capped per frame)
    int loaded = 0;
    for (int dx = -rd; dx <= rd && loaded < MAX_LOADS_PER_FRAME; ++dx)
    {
        for (int dz = -rd; dz <= rd && loaded < MAX_LOADS_PER_FRAME; ++dz)
        {
            glm::ivec2 coord{playerChunk.x + dx, playerChunk.y + dz};
            if (m_chunks.find(coord) == m_chunks.end())
            {
                loadChunk(coord);
                ++loaded;
            }
        }
    }

    // Unload chunks beyond render distance + 2 buffer
    int unloadDist = rd + 2;
    std::vector<glm::ivec2> toUnload;
    for (const auto& [coord, column] : m_chunks)
    {
        int dx = coord.x - playerChunk.x;
        int dz = coord.y - playerChunk.y;
        if (std::abs(dx) > unloadDist || std::abs(dz) > unloadDist)
        {
            toUnload.push_back(coord);
        }
    }
    for (const auto& coord : toUnload)
    {
        unloadChunk(coord);
    }
}

const renderer::ChunkMesh* ChunkManager::getMesh(glm::ivec2 coord, int sectionY) const
{
    auto it = m_meshes.find(MeshKey{coord, sectionY});
    return it != m_meshes.end() ? &it->second : nullptr;
}

void ChunkManager::cleanupCompletedTasks()
{
    auto it = m_inFlightTasks.begin();
    while (it != m_inFlightTasks.end())
    {
        if ((*it)->GetIsComplete())
        {
            MeshKey key{(*it)->input.chunkCoord, (*it)->input.sectionY};
            m_inFlightKeys.erase(key);
            it = m_inFlightTasks.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ChunkManager::pollMeshResults()
{
    for (int i = 0; i < MAX_RESULTS_PER_FRAME; ++i)
    {
        auto result = m_meshResults.tryPop();
        if (!result.has_value())
        {
            break;
        }

        // AC10 — Cancellation: discard result if chunk was unloaded
        if (getChunk(result->chunkCoord) == nullptr)
        {
            continue;
        }

        MeshKey key{result->chunkCoord, result->sectionY};
        m_meshes[key] = std::move(result->mesh);
        m_newMeshKeys.push_back(key);
    }
}

void ChunkManager::consumeNewMeshes(std::vector<MeshReadyEntry>& out)
{
    out.clear();
    for (const auto& key : m_newMeshKeys)
    {
        auto it = m_meshes.find(key);
        if (it != m_meshes.end())
        {
            out.push_back({key, &it->second});
        }
    }
    m_newMeshKeys.clear();
}

void ChunkManager::dispatchDirtySections(const glm::dvec3& playerPos)
{
    // Collect dirty sections with their distance to player
    struct DirtyEntry
    {
        glm::ivec2 coord;
        int sectionY;
        double distSq;
    };
    std::vector<DirtyEntry> dirtySections;

    for (const auto& [coord, column] : m_chunks)
    {
        for (int s = 0; s < ChunkColumn::SECTIONS_PER_COLUMN; ++s)
        {
            if (!column->isSectionDirty(s))
            {
                continue;
            }

            MeshKey key{coord, s};
            if (m_inFlightKeys.contains(key))
            {
                continue;
            }

            // Skip empty sections (no geometry to mesh)
            const ChunkSection* section = column->getSection(s);
            if (section == nullptr || section->isEmpty())
            {
                column->clearDirty(s);
                // Remove stale mesh if section became empty
                m_meshes.erase(key);
                continue;
            }

            // Calculate squared distance from player to section center
            double cx = (coord.x * ChunkSection::SIZE) + ChunkSection::SIZE * 0.5;
            double cy = (s * ChunkSection::SIZE) + ChunkSection::SIZE * 0.5;
            double cz = (coord.y * ChunkSection::SIZE) + ChunkSection::SIZE * 0.5;
            double dx = playerPos.x - cx;
            double dy = playerPos.y - cy;
            double dz = playerPos.z - cz;
            double distSq = dx * dx + dy * dy + dz * dz;

            dirtySections.push_back({coord, s, distSq});
        }
    }

    // Sort by distance (closest first) — AC9
    std::sort(dirtySections.begin(), dirtySections.end(), [](const DirtyEntry& a, const DirtyEntry& b) {
        return a.distSq < b.distSq;
    });

    // Dispatch up to MAX_DISPATCHES_PER_FRAME new tasks
    int dispatched = 0;
    for (const auto& entry : dirtySections)
    {
        if (dispatched >= MAX_DISPATCHES_PER_FRAME)
        {
            break;
        }

        MeshKey key{entry.coord, entry.sectionY};

        // Clear dirty flag BEFORE dispatch (see pitfall #5 in dev notes)
        ChunkColumn* column = getChunk(entry.coord);
        if (column == nullptr)
        {
            continue;
        }
        column->clearDirty(entry.sectionY);

        // Create snapshot
        renderer::MeshJobInput snapshot = createMeshSnapshot(entry.coord, entry.sectionY);

        // Create and dispatch task
        auto task = std::make_unique<renderer::MeshChunkTask>(std::move(snapshot), *m_meshBuilder, m_meshResults);

        // Distance-based priority (AC9): 3 levels (HIGH=0, MED=1, LOW=2)
        // Threshold: < 32 blocks → HIGH, < 128 blocks → MED, else LOW
        if (entry.distSq < 32.0 * 32.0)
        {
            task->m_Priority = enki::TASK_PRIORITY_HIGH;
        }
        else if (entry.distSq < 128.0 * 128.0)
        {
            task->m_Priority = enki::TaskPriority(1);
        }
        else
        {
            task->m_Priority = enki::TaskPriority(enki::TASK_PRIORITY_NUM - 1);
        }

        m_jobSystem->getScheduler().AddTaskSetToPipe(task.get());
        m_inFlightKeys.insert(key);
        m_inFlightTasks.push_back(std::move(task));
        ++dispatched;
    }
}

renderer::MeshJobInput ChunkManager::createMeshSnapshot(glm::ivec2 coord, int sectionY) const
{
    renderer::MeshJobInput input;
    const ChunkColumn* col = getChunk(coord);
    VX_ASSERT(col != nullptr, "Cannot snapshot unloaded chunk");

    const ChunkSection* sec = col->getSection(sectionY);
    if (sec != nullptr)
    {
        input.section = *sec; // Copy by value
    }

    input.chunkCoord = coord;
    input.sectionY = sectionY;

    // Neighbor layout matches BlockFace ordering:
    // [0] PosX (+X) → chunk at (coord.x+1, coord.z), same sectionY
    // [1] NegX (-X) → chunk at (coord.x-1, coord.z), same sectionY
    // [2] PosY (+Y) → same column, sectionY+1
    // [3] NegY (-Y) → same column, sectionY-1
    // [4] PosZ (+Z) → chunk at (coord.x, coord.z+1), same sectionY
    // [5] NegZ (-Z) → chunk at (coord.x, coord.z-1), same sectionY
    struct NeighborOffset
    {
        glm::ivec2 dCoord;
        int dSection;
    };
    static constexpr NeighborOffset offsets[6] = {
        {{1, 0}, 0},  // PosX
        {{-1, 0}, 0}, // NegX
        {{0, 0}, 1},  // PosY (same column, sectionY+1)
        {{0, 0}, -1}, // NegY (same column, sectionY-1)
        {{0, 1}, 0},  // PosZ
        {{0, -1}, 0}, // NegZ
    };

    for (int i = 0; i < 6; ++i)
    {
        glm::ivec2 nCoord = coord + offsets[i].dCoord;
        int nSectionY = sectionY + offsets[i].dSection;

        const ChunkColumn* nCol = getChunk(nCoord);
        if (nCol != nullptr && nSectionY >= 0 && nSectionY < ChunkColumn::SECTIONS_PER_COLUMN)
        {
            const ChunkSection* nSec = nCol->getSection(nSectionY);
            if (nSec != nullptr)
            {
                input.neighbors[i] = *nSec;
                input.hasNeighbor[i] = true;
                continue;
            }
        }
        input.hasNeighbor[i] = false;
    }

    return input;
}

} // namespace voxel::world
