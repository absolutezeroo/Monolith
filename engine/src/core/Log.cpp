#include "voxel/core/Log.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <vector>

namespace voxel::core
{

std::shared_ptr<spdlog::logger> Log::s_logger;

void Log::init()
{
    if (s_logger)
    {
        return; // Reinit guard — do not leak or corrupt on double init
    }

    std::filesystem::create_directories("logs");

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink =
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/voxelforge.log", 5 * 1024 * 1024, 3);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
    s_logger = std::make_shared<spdlog::logger>("VoxelForge", sinks.begin(), sinks.end());

    s_logger->set_pattern("[%H:%M:%S.%e] [%l] [%s:%#] %v");

#ifdef NDEBUG
    s_logger->set_level(spdlog::level::info);
#else
    s_logger->set_level(spdlog::level::trace);
#endif

    s_logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(s_logger);
}

void Log::shutdown()
{
    s_logger.reset();
    spdlog::shutdown();
}

void Log::setLevel(spdlog::level::level_enum level)
{
    if (s_logger)
    {
        s_logger->set_level(level);
    }
}

std::shared_ptr<spdlog::logger>& Log::getLogger()
{
    return s_logger;
}

} // namespace voxel::core
