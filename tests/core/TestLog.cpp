#include "voxel/core/Log.h"

#include <catch2/catch_test_macros.hpp>
#include <spdlog/sinks/ostream_sink.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

void cleanupLogFiles()
{
    std::error_code ec;
    std::filesystem::remove_all("logs", ec);
}

} // namespace

TEST_CASE("Log system initialization", "[core][log]")
{
    cleanupLogFiles();

    SECTION("Log::init() creates logger without crash")
    {
        voxel::core::Log::init();
        REQUIRE(voxel::core::Log::getLogger() != nullptr);
        voxel::core::Log::shutdown();
    }

    SECTION("All log level macros execute without crash")
    {
        voxel::core::Log::init();

        VX_LOG_TRACE("trace message {}", 1);
        VX_LOG_DEBUG("debug message {}", 2);
        VX_LOG_INFO("info message {}", 3);
        VX_LOG_WARN("warn message {}", 4);
        VX_LOG_ERROR("error message {}", 5);
        VX_LOG_CRITICAL("critical message {}", 6);

        REQUIRE(true); // If we get here, no crash occurred
        voxel::core::Log::shutdown();
    }

    cleanupLogFiles();
}

TEST_CASE("Log level filtering", "[core][log]")
{
    cleanupLogFiles();

    SECTION("setLevel filters messages below threshold")
    {
        voxel::core::Log::init();

        // Add an ostream sink to capture output
        auto oss = std::make_shared<std::ostringstream>();
        auto ostreamSink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*oss);
        ostreamSink->set_pattern("%v"); // Just the message
        voxel::core::Log::getLogger()->sinks().push_back(ostreamSink);

        // Set level to warn — info should be filtered
        voxel::core::Log::setLevel(spdlog::level::warn);

        VX_LOG_INFO("this should be filtered");
        voxel::core::Log::getLogger()->flush();

        std::string output = oss->str();
        REQUIRE(output.find("this should be filtered") == std::string::npos);

        // Warn and above should pass through
        VX_LOG_WARN("this should appear");
        voxel::core::Log::getLogger()->flush();

        output = oss->str();
        REQUIRE(output.find("this should appear") != std::string::npos);

        voxel::core::Log::shutdown();
    }

    cleanupLogFiles();
}

TEST_CASE("Log file output", "[core][log]")
{
    cleanupLogFiles();

    SECTION("Rotating file sink creates log file in logs/ directory")
    {
        voxel::core::Log::init();

        VX_LOG_INFO("file output test message");
        voxel::core::Log::getLogger()->flush();
        voxel::core::Log::shutdown();

        REQUIRE(std::filesystem::exists("logs/voxelforge.log"));

        std::ifstream logFile("logs/voxelforge.log");
        std::string content((std::istreambuf_iterator<char>(logFile)), std::istreambuf_iterator<char>());
        REQUIRE(content.find("file output test message") != std::string::npos);
    }

    cleanupLogFiles();
}

TEST_CASE("Log reinit guard", "[core][log]")
{
    cleanupLogFiles();

    SECTION("Calling Log::init() twice does not leak or corrupt")
    {
        voxel::core::Log::init();
        auto firstLogger = voxel::core::Log::getLogger().get();

        voxel::core::Log::init(); // Second call should be a no-op
        auto secondLogger = voxel::core::Log::getLogger().get();

        REQUIRE(firstLogger == secondLogger);

        VX_LOG_INFO("after double init");
        REQUIRE(true); // No crash
        voxel::core::Log::shutdown();
    }

    cleanupLogFiles();
}
