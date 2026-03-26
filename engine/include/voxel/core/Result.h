#pragma once

#include "voxel/core/Types.h"

#include <expected>
#include <string>
#include <string_view>

namespace voxel::core
{

/// Error codes for engine operations.
enum class ErrorCode : uint8
{
    FileNotFound,
    InvalidFormat,
    ShaderCompileError,
    VulkanError,
    ChunkNotLoaded,
    OutOfMemory,
    InvalidArgument,
    ScriptError
};

/// Structured error with code, human-readable message, and optional native result.
struct EngineError
{
    ErrorCode code;
    std::string message;
    int32_t nativeResult = 0;

    EngineError(ErrorCode c, std::string msg = {}, int32_t native = 0)
        : code(c)
        , message(std::move(msg))
        , nativeResult(native)
    {
    }

    /// Factory for Vulkan errors with VkResult context.
    static EngineError vulkan(int32_t vkResult, std::string_view context)
    {
        return {
            ErrorCode::VulkanError,
            std::string(context) + ": VkResult " + std::to_string(vkResult),
            vkResult};
    }

    /// Factory for file-not-found errors with path context.
    static EngineError file(std::string_view path)
    {
        return {ErrorCode::FileNotFound, "File not found: " + std::string(path)};
    }
};

/// Result type for fallible operations.
/// Success: holds T. Failure: holds EngineError.
template <typename T> using Result = std::expected<T, EngineError>;

} // namespace voxel::core
