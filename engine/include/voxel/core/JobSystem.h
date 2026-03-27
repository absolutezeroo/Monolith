#pragma once

#include "voxel/core/Result.h"

#include <enkiTS/TaskScheduler.h>

#include <cstdint>

namespace voxel::core
{

/// Wraps enkiTS TaskScheduler. Single instance owned by GameApp, passed by reference.
/// Thread-safe for task submission from any thread after init().
class JobSystem
{
  public:
    JobSystem() = default;
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /// Initialize the task scheduler. numThreads=0 means auto-detect.
    Result<void> init(uint32_t numThreads = 0);

    /// Wait for all tasks and shut down. Safe to call if not initialized.
    void shutdown();

    /// Access the underlying scheduler for task submission.
    [[nodiscard]] enki::TaskScheduler& getScheduler();

    /// Number of worker threads (not counting the main thread).
    [[nodiscard]] uint32_t threadCount() const;

    /// Whether init() has been called successfully.
    [[nodiscard]] bool isInitialized() const;

  private:
    enki::TaskScheduler m_scheduler;
    bool m_initialized = false;
};

} // namespace voxel::core
