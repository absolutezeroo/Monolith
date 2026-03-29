#pragma once

#include "voxel/core/ConcurrentQueue.h"
#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/world/ChunkSection.h"
#include "voxel/world/LightMap.h"

#include <enkiTS/TaskScheduler.h>
#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

namespace voxel::renderer
{

/// Immutable snapshot of a section + neighbors for async meshing.
/// All data is COPIED by value — safe to read from any thread.
struct MeshJobInput
{
    world::ChunkSection section;                  // Center section (full copy, ~8KB)
    std::array<world::ChunkSection, 6> neighbors; // Neighbor sections (copied)
    std::array<bool, 6> hasNeighbor = {};          // Which neighbors were present
    glm::ivec2 chunkCoord{0, 0};                  // Chunk XZ coordinate
    int sectionY = 0;                              // Section index within column (0-15)

    // Light data snapshot (4KB each × 7 = 28KB when present)
    world::LightMap lightData;                         // Center section light (zero = default sky=15)
    std::array<world::LightMap, 6> neighborLightData;  // Neighbor light data (copied)
    std::array<bool, 6> hasNeighborLight = {};          // Which neighbor light maps were present
    bool hasLightData = false;                          // When false, mesher skips light averaging
};

/// Result produced by an async mesh task.
struct MeshResult
{
    ChunkMesh mesh;
    glm::ivec2 chunkCoord{0, 0};
    int sectionY = 0;
};

/// enkiTS task that meshes a section snapshot on a worker thread.
/// Produces a MeshResult and pushes it to a ConcurrentQueue.
/// Task must remain alive until completed — caller manages lifetime.
struct MeshChunkTask : enki::ITaskSet
{
    MeshJobInput input;
    const MeshBuilder* meshBuilder = nullptr;
    core::ConcurrentQueue<MeshResult>* resultQueue = nullptr;

    MeshChunkTask(
        MeshJobInput&& in,
        const MeshBuilder& builder,
        core::ConcurrentQueue<MeshResult>& queue)
        : input(std::move(in))
        , meshBuilder(&builder)
        , resultQueue(&queue)
    {
        m_SetSize = 1; // Single execution, not a parallel-for
    }

    void ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/) override
    {
        // Reconstruct neighbor pointer array from snapshot
        std::array<const world::ChunkSection*, 6> neighbors{};
        for (int i = 0; i < 6; ++i)
        {
            neighbors[i] = input.hasNeighbor[i] ? &input.neighbors[i] : nullptr;
        }

        // Reconstruct light map pointer arrays from snapshot
        const world::LightMap* lightMap = input.hasLightData ? &input.lightData : nullptr;
        std::array<const world::LightMap*, 6> neighborLightMaps{};
        if (input.hasLightData)
        {
            for (int i = 0; i < 6; ++i)
            {
                neighborLightMaps[i] = input.hasNeighborLight[i] ? &input.neighborLightData[i] : nullptr;
            }
        }

        MeshResult result;
        result.mesh = meshBuilder->buildGreedy(input.section, neighbors, lightMap, neighborLightMaps);
        result.chunkCoord = input.chunkCoord;
        result.sectionY = input.sectionY;

        resultQueue->push(std::move(result));
    }
};

} // namespace voxel::renderer
