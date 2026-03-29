#include "voxel/renderer/ChunkUploadManager.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/ChunkRenderInfoBuffer.h"
#include "voxel/renderer/Gigabuffer.h"
#include "voxel/renderer/StagingBuffer.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ChunkSection.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace voxel::renderer
{

ChunkUploadManager::ChunkUploadManager(
    Gigabuffer& gigabuffer,
    StagingBuffer& stagingBuffer,
    ChunkRenderInfoBuffer& chunkRenderInfo)
    : m_gigabuffer(gigabuffer), m_stagingBuffer(stagingBuffer), m_chunkRenderInfoBuffer(chunkRenderInfo)
{
}

void ChunkUploadManager::processUploads(world::ChunkManager& chunkManager, const glm::dvec3& playerPos)
{
    // 1. Consume new meshes from ChunkManager
    std::vector<world::ChunkManager::MeshReadyEntry> newMeshes;
    chunkManager.consumeNewMeshes(newMeshes);

    for (const auto& entry : newMeshes)
    {
        SectionKey skey{entry.key.coord, entry.key.sectionY};

        // Empty mesh (no quads) — store None state, no allocation (AC9)
        if (entry.mesh->isEmpty())
        {
            // If there was an existing Resident allocation, queue deferred free
            auto it = m_renderInfos.find(skey);
            if (it != m_renderInfos.end() && it->second.state == RenderState::Resident)
            {
                queueDeferredFree(it->second.allocation);
                if (it->second.transAllocation.size > 0)
                {
                    queueDeferredFree(it->second.transAllocation);
                }
            }

            // Free GPU slot if this section had one (prevents slot leak on empty remesh)
            auto slotIt = m_slotMap.find(skey);
            if (slotIt != m_slotMap.end())
            {
                m_chunkRenderInfoBuffer.freeSlot(slotIt->second);
                m_slotMap.erase(slotIt);
            }

            m_renderInfos[skey] = ChunkRenderInfo{
                .allocation = {},
                .quadCount = 0,
                .worldBasePos = glm::ivec3{0},
                .state = RenderState::None,
            };
            continue;
        }

        // If existing Resident allocation for this section, queue deferred free (remesh)
        auto it = m_renderInfos.find(skey);
        if (it != m_renderInfos.end() && it->second.state == RenderState::Resident)
        {
            queueDeferredFree(it->second.allocation);
            if (it->second.transAllocation.size > 0)
            {
                queueDeferredFree(it->second.transAllocation);
            }
        }

        // Calculate distance to player for priority
        double cx = entry.key.coord.x * world::ChunkSection::SIZE + world::ChunkSection::SIZE * 0.5;
        double cy = entry.key.sectionY * world::ChunkSection::SIZE + world::ChunkSection::SIZE * 0.5;
        double cz = entry.key.coord.y * world::ChunkSection::SIZE + world::ChunkSection::SIZE * 0.5;
        double dx = playerPos.x - cx;
        double dy = playerPos.y - cy;
        double dz = playerPos.z - cz;
        auto distSq = static_cast<float>(dx * dx + dy * dy + dz * dz);

        m_pendingUploads.push_back(PendingUpload{
            .key = skey,
            .mesh = *entry.mesh,
            .distanceSq = distSq,
        });
    }

    // 2. Sort pending uploads by distance (closest first)
    std::sort(m_pendingUploads.begin(), m_pendingUploads.end(), [](const PendingUpload& a, const PendingUpload& b) {
        return a.distanceSq < b.distanceSq;
    });

    // 3. Drain up to MAX_UPLOADS_PER_FRAME
    uint32_t uploaded = 0;
    auto it = m_pendingUploads.begin();
    while (it != m_pendingUploads.end() && uploaded < MAX_UPLOADS_PER_FRAME)
    {
        bool success = uploadSingle(it->key, it->mesh);
        if (success)
        {
            it = m_pendingUploads.erase(it);
            ++uploaded;
        }
        else
        {
            // Staging buffer full this frame — stop draining, try next frame
            break;
        }
    }
}

bool ChunkUploadManager::uploadSingle(const SectionKey& key, const ChunkMesh& mesh)
{
    // Rate-limit check before any allocations
    if (m_stagingBuffer.pendingTransferCount() >= MAX_UPLOADS_PER_FRAME)
    {
        return false; // Staging budget exhausted — retry next frame
    }

    // 1. Upload opaque quads + light data (if any)
    // Layout: [quad0_lo, quad0_hi, quad1_lo, ... | light0, light1, ...]
    GigabufferAllocation opaqueAlloc{};
    uint32_t opaqueCount = 0;
    if (mesh.quadCount > 0)
    {
        VkDeviceSize quadBytes = mesh.quadCount * sizeof(uint64_t);
        VkDeviceSize lightBytes = mesh.quadCount * sizeof(uint32_t);
        VkDeviceSize uploadSize = quadBytes + lightBytes;
        auto allocResult = m_gigabuffer.allocate(uploadSize);
        if (!allocResult.has_value())
        {
            VX_LOG_WARN(
                "Gigabuffer full — cannot upload section ({},{}) y={}",
                key.chunkCoord.x,
                key.chunkCoord.y,
                key.sectionY);
            m_renderInfos[key] = ChunkRenderInfo{
                .allocation = {},
                .quadCount = 0,
                .transAllocation = {},
                .transQuadCount = 0,
                .worldBasePos = glm::ivec3{0},
                .state = RenderState::None,
            };
            return true; // consumed
        }

        // Combine quad + light data into a single staging upload to ensure atomicity.
        // Prevents garbage light data if staging is exhausted between separate uploads.
        std::vector<uint8_t> combined(static_cast<size_t>(uploadSize));
        std::memcpy(combined.data(), mesh.quads.data(), static_cast<size_t>(quadBytes));
        std::memcpy(combined.data() + quadBytes, mesh.quadLightData.data(), static_cast<size_t>(lightBytes));

        auto uploadResult = m_stagingBuffer.uploadToGigabuffer(
            combined.data(), uploadSize, allocResult->offset);
        if (!uploadResult.has_value())
        {
            m_gigabuffer.free(*allocResult);
            return false; // Retry next frame
        }

        opaqueAlloc = *allocResult;
        opaqueCount = mesh.quadCount;
    }

    // 2. Upload translucent quads + light data (if any)
    GigabufferAllocation transAlloc{};
    uint32_t transCount = 0;
    if (mesh.translucentQuadCount > 0)
    {
        VkDeviceSize transQuadBytes = mesh.translucentQuadCount * sizeof(uint64_t);
        VkDeviceSize transLightBytes = mesh.translucentQuadCount * sizeof(uint32_t);
        VkDeviceSize transSize = transQuadBytes + transLightBytes;
        auto transAllocResult = m_gigabuffer.allocate(transSize);
        if (!transAllocResult.has_value())
        {
            VX_LOG_WARN(
                "Gigabuffer full for translucent — section ({},{}) y={} opaque only",
                key.chunkCoord.x,
                key.chunkCoord.y,
                key.sectionY);
        }
        else
        {
            // Combine quad + light data into a single staging upload
            std::vector<uint8_t> transCombined(static_cast<size_t>(transSize));
            std::memcpy(transCombined.data(), mesh.translucentQuads.data(), static_cast<size_t>(transQuadBytes));
            std::memcpy(
                transCombined.data() + transQuadBytes,
                mesh.translucentQuadLightData.data(),
                static_cast<size_t>(transLightBytes));

            auto transUpload = m_stagingBuffer.uploadToGigabuffer(
                transCombined.data(), transSize, transAllocResult->offset);
            if (!transUpload.has_value())
            {
                VX_LOG_WARN(
                    "Staging full for translucent — section ({},{}) y={} opaque only",
                    key.chunkCoord.x,
                    key.chunkCoord.y,
                    key.sectionY);
                m_gigabuffer.free(*transAllocResult);
            }
            else
            {
                transAlloc = *transAllocResult;
                transCount = mesh.translucentQuadCount;
            }
        }
    }

    // 3. Store render info
    ChunkRenderInfo renderInfo{
        .allocation = opaqueAlloc,
        .quadCount = opaqueCount,
        .transAllocation = transAlloc,
        .transQuadCount = transCount,
        .worldBasePos = glm::ivec3{
            key.chunkCoord.x * world::ChunkSection::SIZE,
            key.sectionY * world::ChunkSection::SIZE,
            key.chunkCoord.y * world::ChunkSection::SIZE,
        },
        .state = RenderState::Resident,
    };
    m_renderInfos[key] = renderInfo;

    // 4. Update ChunkRenderInfoBuffer GPU slot (cull.comp works on quadCount for opaque, transQuadCount for translucent)
    auto slotIt = m_slotMap.find(key);
    if (slotIt != m_slotMap.end())
    {
        // Remesh: reuse existing slot, just update data
        m_chunkRenderInfoBuffer.update(slotIt->second, buildGpuInfo(renderInfo));
    }
    else
    {
        // New section: allocate a slot
        auto slotResult = m_chunkRenderInfoBuffer.allocateSlot();
        if (slotResult.has_value())
        {
            m_slotMap[key] = *slotResult;
            m_chunkRenderInfoBuffer.update(*slotResult, buildGpuInfo(renderInfo));
        }
        else
        {
            VX_LOG_WARN(
                "ChunkRenderInfoBuffer full — section ({},{}) y={} has no GPU slot",
                key.chunkCoord.x,
                key.chunkCoord.y,
                key.sectionY);
        }
    }

    return true;
}

void ChunkUploadManager::processDeferredFrees()
{
    // Iterate backwards for safe swap-and-pop removal.
    // Decrement first, then free at zero — matches AC5: freed after FRAMES_IN_FLIGHT ticks.
    for (int i = static_cast<int>(m_deferredFrees.size()) - 1; i >= 0; --i)
    {
        auto& entry = m_deferredFrees[static_cast<size_t>(i)];
        --entry.framesRemaining;
        if (entry.framesRemaining == 0)
        {
            m_gigabuffer.free(entry.allocation);
            // Swap-and-pop O(1) removal
            m_deferredFrees[static_cast<size_t>(i)] = m_deferredFrees.back();
            m_deferredFrees.pop_back();
        }
    }
}

void ChunkUploadManager::onChunkUnloaded(glm::ivec2 coord)
{
    for (int s = 0; s < world::ChunkColumn::SECTIONS_PER_COLUMN; ++s)
    {
        SectionKey skey{coord, s};

        // Queue deferred free for any Resident sections
        auto it = m_renderInfos.find(skey);
        if (it != m_renderInfos.end() && it->second.state == RenderState::Resident)
        {
            queueDeferredFree(it->second.allocation);
            if (it->second.transAllocation.size > 0)
            {
                queueDeferredFree(it->second.transAllocation);
            }
        }
        m_renderInfos.erase(skey);

        // Free GPU slot in ChunkRenderInfoBuffer
        auto slotIt = m_slotMap.find(skey);
        if (slotIt != m_slotMap.end())
        {
            m_chunkRenderInfoBuffer.freeSlot(slotIt->second);
            m_slotMap.erase(slotIt);
        }
    }

    // Remove any pending uploads for this chunk
    auto pit = std::remove_if(
        m_pendingUploads.begin(),
        m_pendingUploads.end(),
        [&coord](const PendingUpload& pu) { return pu.key.chunkCoord == coord; });
    m_pendingUploads.erase(pit, m_pendingUploads.end());
}

const ChunkRenderInfo* ChunkUploadManager::getRenderInfo(const SectionKey& key) const
{
    auto it = m_renderInfos.find(key);
    return it != m_renderInfos.end() ? &it->second : nullptr;
}

uint32_t ChunkUploadManager::residentCount() const
{
    uint32_t count = 0;
    for (const auto& [key, info] : m_renderInfos)
    {
        if (info.state == RenderState::Resident)
        {
            ++count;
        }
    }
    return count;
}

uint32_t ChunkUploadManager::pendingUploadCount() const
{
    return static_cast<uint32_t>(m_pendingUploads.size());
}

uint32_t ChunkUploadManager::deferredFreeCount() const
{
    return static_cast<uint32_t>(m_deferredFrees.size());
}

void ChunkUploadManager::queueDeferredFree(const GigabufferAllocation& allocation)
{
    m_deferredFrees.push_back(DeferredFree{.allocation = allocation, .framesRemaining = FRAMES_IN_FLIGHT});
}

} // namespace voxel::renderer
