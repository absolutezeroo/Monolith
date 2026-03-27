#pragma once

#include "voxel/core/ConcurrentQueue.h"
#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshJobTypes.h"
#include "voxel/world/ChunkColumn.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace voxel::core
{
class JobSystem;
}

namespace voxel::renderer
{
class MeshBuilder;
}

namespace voxel::world
{

/// Floor division — correct for negative dividends (C++ truncates toward zero).
inline int floorDiv(int a, int b)
{
    return a / b - (a % b != 0 && (a ^ b) < 0);
}

/// Euclidean modulo — always returns a non-negative result.
inline int euclideanMod(int a, int b)
{
    int r = a % b;
    return r + (r < 0) * b;
}

/// Translate world position to chunk column coordinate (X/Z only).
inline glm::ivec2 worldToChunkCoord(const glm::ivec3& worldPos)
{
    return {floorDiv(worldPos.x, ChunkSection::SIZE), floorDiv(worldPos.z, ChunkSection::SIZE)};
}

/// Translate world position to chunk-local position (Y passed through).
inline glm::ivec3 worldToLocalPos(const glm::ivec3& worldPos)
{
    return {euclideanMod(worldPos.x, ChunkSection::SIZE), worldPos.y, euclideanMod(worldPos.z, ChunkSection::SIZE)};
}

/// Spatial hash for glm::ivec2 chunk coordinates using XOR-shift combination.
struct ChunkCoordHash
{
    size_t operator()(const glm::ivec2& v) const noexcept
    {
        size_t h = std::hash<int>{}(v.x);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class WorldGenerator;

/// Key identifying a specific section within the world (chunk coord + section Y index).
struct MeshKey
{
    glm::ivec2 coord{0, 0};
    int sectionY = 0;

    bool operator==(const MeshKey& other) const { return coord == other.coord && sectionY == other.sectionY; }
};

/// Hash for MeshKey used in unordered containers.
struct MeshKeyHash
{
    size_t operator()(const MeshKey& k) const noexcept
    {
        ChunkCoordHash ch;
        size_t h = ch(k.coord);
        h ^= std::hash<int>{}(k.sectionY) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class ChunkManager
{
  public:
    static constexpr int MAX_RESULTS_PER_FRAME = 8;
    static constexpr int MAX_DISPATCHES_PER_FRAME = 4;

    ChunkManager() = default;

    /// Set the world generator used for creating new chunks. Non-owning pointer.
    void setWorldGenerator(WorldGenerator* worldGen) { m_worldGen = worldGen; }

    /// Set the job system for async meshing. Non-owning pointer.
    void setJobSystem(core::JobSystem* jobSystem) { m_jobSystem = jobSystem; }

    /// Set the mesh builder for async meshing. Non-owning pointer.
    void setMeshBuilder(const renderer::MeshBuilder* meshBuilder) { m_meshBuilder = meshBuilder; }

    /// Returns the ChunkColumn at the given coordinate, or nullptr if not loaded.
    [[nodiscard]] ChunkColumn* getChunk(glm::ivec2 coord);
    [[nodiscard]] const ChunkColumn* getChunk(glm::ivec2 coord) const;

    /// Get block at world position. Returns BLOCK_AIR if chunk not loaded.
    [[nodiscard]] uint16_t getBlock(const glm::ivec3& worldPos) const;

    /// Set block at world position. No-op if chunk not loaded (debug assert).
    /// Also marks affected neighbor sections dirty for remeshing.
    void setBlock(const glm::ivec3& worldPos, uint16_t id);

    /// Insert a new empty ChunkColumn. No-op if already loaded.
    void loadChunk(glm::ivec2 coord);

    /// Remove a ChunkColumn. No-op if not loaded.
    void unloadChunk(glm::ivec2 coord);

    /// Number of currently loaded chunk columns.
    [[nodiscard]] size_t loadedChunkCount() const;

    /// Number of chunk columns with at least one dirty section.
    [[nodiscard]] size_t dirtyChunkCount() const;

    /// Main-thread update: poll completed mesh results, dispatch dirty sections for async meshing.
    /// playerPos is used for distance-based priority.
    void update(const glm::dvec3& playerPos);

    /// Retrieve a completed mesh for a given section, or nullptr if not yet available.
    [[nodiscard]] const renderer::ChunkMesh* getMesh(glm::ivec2 coord, int sectionY) const;

  private:
    /// Create an immutable snapshot of a section and its 6 neighbors for async meshing.
    [[nodiscard]] renderer::MeshJobInput createMeshSnapshot(glm::ivec2 coord, int sectionY) const;

    /// Clean up completed tasks from the in-flight list.
    void cleanupCompletedTasks();

    /// Poll completed mesh results from the concurrent queue.
    void pollMeshResults();

    /// Dispatch dirty sections for async meshing, sorted by distance to player.
    void dispatchDirtySections(const glm::dvec3& playerPos);

    std::unordered_map<glm::ivec2, std::unique_ptr<ChunkColumn>, ChunkCoordHash> m_chunks;
    WorldGenerator* m_worldGen = nullptr;

    // Async meshing infrastructure
    core::JobSystem* m_jobSystem = nullptr;
    const renderer::MeshBuilder* m_meshBuilder = nullptr;
    core::ConcurrentQueue<renderer::MeshResult> m_meshResults;

    // Completed mesh storage
    std::unordered_map<MeshKey, renderer::ChunkMesh, MeshKeyHash> m_meshes;

    // In-flight task tracking
    std::vector<std::unique_ptr<renderer::MeshChunkTask>> m_inFlightTasks;
    std::unordered_set<MeshKey, MeshKeyHash> m_inFlightKeys;
};

} // namespace voxel::world
