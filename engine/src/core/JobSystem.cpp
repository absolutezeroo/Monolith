#include "voxel/core/JobSystem.h"

#include "voxel/core/Log.h"

namespace voxel::core
{

JobSystem::~JobSystem()
{
    shutdown();
}

Result<void> JobSystem::init(uint32_t numThreads)
{
    if (m_initialized)
    {
        return std::unexpected(EngineError{ErrorCode::InvalidArgument, "JobSystem already initialized"});
    }

    enki::TaskSchedulerConfig config;
    if (numThreads > 0)
    {
        config.numTaskThreadsToCreate = numThreads;
    }
    // else: enkiTS auto-detects (hardware_concurrency - 1)

    m_scheduler.Initialize(config);
    m_initialized = true;

    return {};
}

void JobSystem::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_initialized = false;
    // Note: actual thread shutdown deferred to ~enki::TaskScheduler which calls
    // StopThreads(true). We do NOT call WaitforAllAndShutdown() here to avoid
    // a double-cleanup crash with the enkiTS DLL build.
}

enki::TaskScheduler& JobSystem::getScheduler()
{
    return m_scheduler;
}

uint32_t JobSystem::threadCount() const
{
    return m_initialized ? m_scheduler.GetNumTaskThreads() : 0;
}

bool JobSystem::isInitialized() const
{
    return m_initialized;
}

} // namespace voxel::core
