#pragma once

#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/ChunkRenderInfo.h"
#include "voxel/renderer/RendererConstants.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace voxel::world
{
class ChunkManager;
}

namespace voxel::renderer
{

class ChunkRenderInfoBuffer;
class Gigabuffer;
class StagingBuffer;

/**
 * @brief Orchestrates CPU→GPU mesh upload via Gigabuffer + StagingBuffer.
 *
 * Polls ChunkManager for new meshes, allocates gigabuffer space, stages uploads,
 * and manages deferred frees for safe remesh/unload. Also maintains GPU slot
 * allocations in ChunkRenderInfoBuffer for compute culling. All operations are main-thread only.
 */
class ChunkUploadManager
{
  public:
    static constexpr uint32_t MAX_UPLOADS_PER_FRAME = 8;

    ChunkUploadManager(Gigabuffer& gigabuffer, StagingBuffer& stagingBuffer, ChunkRenderInfoBuffer& chunkRenderInfo);

    /// Poll ChunkManager for new meshes, allocate gigabuffer space, stage uploads.
    /// Call once per frame from the main thread.
    void processUploads(world::ChunkManager& chunkManager, const glm::dvec3& playerPos);

    /// Tick deferred free queue. Call once per frame.
    void processDeferredFrees();

    /// Called when a chunk is unloaded — queues all section allocations for deferred free.
    void onChunkUnloaded(glm::ivec2 coord);

    [[nodiscard]] const ChunkRenderInfoMap& getAllRenderInfos() const { return m_renderInfos; }
    [[nodiscard]] const ChunkRenderInfo* getRenderInfo(const SectionKey& key) const;
    [[nodiscard]] uint32_t residentCount() const;
    [[nodiscard]] uint32_t pendingUploadCount() const;
    [[nodiscard]] uint32_t deferredFreeCount() const;

  private:
    struct PendingUpload
    {
        SectionKey key;
        ChunkMesh mesh;
        float distanceSq = 0.0f;
    };

    struct DeferredFree
    {
        GigabufferAllocation allocation;
        uint32_t framesRemaining = FRAMES_IN_FLIGHT;
    };

    /// Upload a single mesh to the gigabuffer via the staging buffer.
    /// Returns true on success, false if staging buffer is full this frame.
    bool uploadSingle(const SectionKey& key, const ChunkMesh& mesh);

    void queueDeferredFree(const GigabufferAllocation& allocation);

    Gigabuffer& m_gigabuffer;
    StagingBuffer& m_stagingBuffer;
    ChunkRenderInfoBuffer& m_chunkRenderInfoBuffer;

    ChunkRenderInfoMap m_renderInfos;
    std::vector<PendingUpload> m_pendingUploads;
    std::vector<DeferredFree> m_deferredFrees;

    /// Maps SectionKey → GPU slot index in ChunkRenderInfoBuffer.
    std::unordered_map<SectionKey, uint32_t, SectionKeyHash> m_slotMap;
};

} // namespace voxel::renderer
