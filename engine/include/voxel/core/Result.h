#pragma once

#include "voxel/core/Types.h"

#include <expected>

namespace voxel::core
{

/// Error codes for engine operations.
enum class EngineError : uint8
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

/// Result type for fallible operations.
/// Success: holds T. Failure: holds EngineError.
template <typename T> using Result = std::expected<T, EngineError>;

} // namespace voxel::core
