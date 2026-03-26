#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace voxel::core
{

class Log
{
  public:
    static void init();
    static void shutdown();
    static void setLevel(spdlog::level::level_enum level);
    static std::shared_ptr<spdlog::logger>& getLogger();

  private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace voxel::core

// Macros MUST use SPDLOG_LOGGER_* for source location support ([source:line] in output)
#define VX_LOG_TRACE(...) SPDLOG_LOGGER_TRACE(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_INFO(...) SPDLOG_LOGGER_INFO(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_WARN(...) SPDLOG_LOGGER_WARN(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_ERROR(...) SPDLOG_LOGGER_ERROR(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::voxel::core::Log::getLogger(), __VA_ARGS__)
