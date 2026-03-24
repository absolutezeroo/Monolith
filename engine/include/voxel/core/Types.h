#pragma once

#include <cstddef>
#include <cstdint>

namespace voxel::core
{

// Fixed-width integer aliases
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// Size and pointer types
using usize = std::size_t;
using isize = std::ptrdiff_t;

// Floating-point aliases (explicit width documentation)
using float32 = float;
using float64 = double;

} // namespace voxel::core
