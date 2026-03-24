#pragma once

#include "voxel/core/Types.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace voxel::math
{

// Floating-point vector types
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;

// Double-precision vector types
using DVec2 = glm::dvec2;
using DVec3 = glm::dvec3;
using DVec4 = glm::dvec4;

// Integer vector types
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;

// Unsigned integer vector types
using UVec2 = glm::uvec2;
using UVec3 = glm::uvec3;
using UVec4 = glm::uvec4;

// Matrix types
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

} // namespace voxel::math
