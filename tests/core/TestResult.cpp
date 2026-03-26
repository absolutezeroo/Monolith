#include "voxel/core/Result.h"

#include <catch2/catch_test_macros.hpp>

using namespace voxel::core;

TEST_CASE("Result<T> success and error construction", "[core][result]")
{
    SECTION("Success construction holds value")
    {
        Result<int> r = 42;
        REQUIRE(r.has_value());
        REQUIRE(r.value() == 42);
    }

    SECTION("Error construction via std::unexpected")
    {
        Result<int> r = std::unexpected(EngineError{ErrorCode::FileNotFound});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::FileNotFound);
    }
}

TEST_CASE("Result<T> monadic operations", "[core][result]")
{
    SECTION(".and_then() chains on success")
    {
        Result<int> r = 10;
        auto doubled = r.and_then([](int v) -> Result<int> { return v * 2; });
        REQUIRE(doubled.has_value());
        REQUIRE(doubled.value() == 20);
    }

    SECTION(".and_then() short-circuits on error")
    {
        Result<int> r = std::unexpected(EngineError{ErrorCode::OutOfMemory});
        bool called = false;
        auto result = r.and_then(
            [&](int v) -> Result<int>
            {
                called = true;
                return v * 2;
            });
        REQUIRE_FALSE(called);
        REQUIRE(result.error().code == ErrorCode::OutOfMemory);
    }

    SECTION(".or_else() invoked on error")
    {
        Result<int> r = std::unexpected(EngineError{ErrorCode::FileNotFound});
        auto recovered = r.or_else([](const EngineError&) -> Result<int> { return 0; });
        REQUIRE(recovered.has_value());
        REQUIRE(recovered.value() == 0);
    }

    SECTION(".or_else() skipped on success")
    {
        Result<int> r = 42;
        bool called = false;
        auto result = r.or_else(
            [&](const EngineError&) -> Result<int>
            {
                called = true;
                return 0;
            });
        REQUIRE_FALSE(called);
        REQUIRE(result.value() == 42);
    }

    SECTION(".transform() maps value on success")
    {
        Result<int> r = 5;
        auto mapped = r.transform([](int v) { return v * 3; });
        REQUIRE(mapped.has_value());
        REQUIRE(mapped.value() == 15);
    }
}

TEST_CASE("std::unexpected propagation through chain", "[core][result]")
{
    auto step1 = [](int v) -> Result<int>
    {
        return v + 1;
    };
    auto step2 = [](int) -> Result<int>
    {
        return std::unexpected(EngineError{ErrorCode::InvalidFormat});
    };
    auto step3 = [](int v) -> Result<int>
    {
        return v * 10;
    };

    Result<int> r = 1;
    auto finalResult = r.and_then(step1).and_then(step2).and_then(step3);

    REQUIRE_FALSE(finalResult.has_value());
    REQUIRE(finalResult.error().code == ErrorCode::InvalidFormat);
}

TEST_CASE("ErrorCode covers all defined variants", "[core][result]")
{
    // Verify all enum values are distinct and constructable
    REQUIRE(ErrorCode::FileNotFound != ErrorCode::InvalidFormat);
    REQUIRE(ErrorCode::ShaderCompileError != ErrorCode::VulkanError);
    REQUIRE(ErrorCode::ChunkNotLoaded != ErrorCode::OutOfMemory);
    REQUIRE(ErrorCode::ScriptError != ErrorCode::FileNotFound);

    // Verify underlying type is uint8
    REQUIRE(sizeof(ErrorCode) == sizeof(uint8));
}

TEST_CASE("EngineError struct carries context", "[core][result]")
{
    SECTION("vulkan factory stores VkResult and context")
    {
        auto err = EngineError::vulkan(-3, "vkCreateDevice failed");
        REQUIRE(err.code == ErrorCode::VulkanError);
        REQUIRE(err.nativeResult == -3);
        REQUIRE(err.message.find("vkCreateDevice failed") != std::string::npos);
        REQUIRE(err.message.find("-3") != std::string::npos);
    }

    SECTION("file factory stores path")
    {
        auto err = EngineError::file("assets/missing.json");
        REQUIRE(err.code == ErrorCode::FileNotFound);
        REQUIRE(err.message.find("assets/missing.json") != std::string::npos);
    }

    SECTION("direct construction with code and message")
    {
        EngineError err{ErrorCode::InvalidArgument, "bad input"};
        REQUIRE(err.code == ErrorCode::InvalidArgument);
        REQUIRE(err.message == "bad input");
        REQUIRE(err.nativeResult == 0);
    }
}
