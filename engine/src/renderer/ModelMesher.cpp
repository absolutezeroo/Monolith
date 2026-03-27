#include "voxel/renderer/ModelMesher.h"

#include <glm/glm.hpp>

namespace voxel::renderer
{

void ModelMesher::emitQuad(
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& v2,
    const glm::vec3& v3,
    const glm::vec3& normal,
    uint16_t blockStateId,
    uint8_t ao,
    uint8_t flags,
    std::vector<ModelVertex>& outVertices)
{
    // UV mapping: v0=(0,0), v1=(1,0), v2=(1,1), v3=(0,1)
    auto makeVert = [&](const glm::vec3& pos, const glm::vec2& uv) -> ModelVertex {
        return ModelVertex{pos, normal, uv, blockStateId, ao, flags};
    };

    // Triangle 1: v0, v1, v2
    outVertices.push_back(makeVert(v0, {0.0f, 0.0f}));
    outVertices.push_back(makeVert(v1, {1.0f, 0.0f}));
    outVertices.push_back(makeVert(v2, {1.0f, 1.0f}));

    // Triangle 2: v0, v2, v3
    outVertices.push_back(makeVert(v0, {0.0f, 0.0f}));
    outVertices.push_back(makeVert(v2, {1.0f, 1.0f}));
    outVertices.push_back(makeVert(v3, {0.0f, 1.0f}));
}

void ModelMesher::emitBox(
    const glm::vec3& offset,
    const glm::vec3& minCorner,
    const glm::vec3& maxCorner,
    uint16_t blockStateId,
    uint8_t ao,
    uint8_t flags,
    std::vector<ModelVertex>& outVertices)
{
    float x0 = offset.x + minCorner.x;
    float y0 = offset.y + minCorner.y;
    float z0 = offset.z + minCorner.z;
    float x1 = offset.x + maxCorner.x;
    float y1 = offset.y + maxCorner.y;
    float z1 = offset.z + maxCorner.z;

    // +X face
    emitQuad(
        {x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0},
        {1.0f, 0.0f, 0.0f}, blockStateId, ao, flags, outVertices);

    // -X face
    emitQuad(
        {x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1},
        {-1.0f, 0.0f, 0.0f}, blockStateId, ao, flags, outVertices);

    // +Y face
    emitQuad(
        {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1},
        {0.0f, 1.0f, 0.0f}, blockStateId, ao, flags, outVertices);

    // -Y face
    emitQuad(
        {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0},
        {0.0f, -1.0f, 0.0f}, blockStateId, ao, flags, outVertices);

    // +Z face
    emitQuad(
        {x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1},
        {0.0f, 0.0f, 1.0f}, blockStateId, ao, flags, outVertices);

    // -Z face
    emitQuad(
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {0.0f, 0.0f, -1.0f}, blockStateId, ao, flags, outVertices);
}

void ModelMesher::generateSlab(
    int x,
    int y,
    int z,
    const world::BlockDefinition& blockDef,
    const world::StateMap& state,
    uint8_t ao,
    std::vector<ModelVertex>& outVertices)
{
    auto it = state.find("half");
    bool isTop = (it != state.end() && it->second == "top");

    glm::vec3 offset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    glm::vec3 minCorner = isTop ? glm::vec3(0.0f, 0.5f, 0.0f) : glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 maxCorner = isTop ? glm::vec3(1.0f, 1.0f, 1.0f) : glm::vec3(1.0f, 0.5f, 1.0f);

    uint8_t flags = static_cast<uint8_t>((blockDef.tintIndex & 0x1) | ((blockDef.waving & 0x3) << 1));

    emitBox(offset, minCorner, maxCorner, blockDef.baseStateId, ao, flags, outVertices);
}

void ModelMesher::generateCross(
    int x,
    int y,
    int z,
    const world::BlockDefinition& blockDef,
    std::vector<ModelVertex>& outVertices)
{
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    float fz = static_cast<float>(z);

    uint8_t flags = static_cast<uint8_t>((blockDef.tintIndex & 0x1) | ((blockDef.waving & 0x3) << 1));
    uint16_t stateId = blockDef.baseStateId;

    // Diagonal quad normals (perpendicular to each diagonal plane)
    glm::vec3 normalA = glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f));
    glm::vec3 normalB = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));

    // Quad A: diagonal from (-X,-Z) to (+X,+Z) — front
    emitQuad(
        {fx + 0.0f, fy + 0.0f, fz + 0.0f},
        {fx + 1.0f, fy + 0.0f, fz + 1.0f},
        {fx + 1.0f, fy + 1.0f, fz + 1.0f},
        {fx + 0.0f, fy + 1.0f, fz + 0.0f},
        normalA, stateId, 3, flags, outVertices);

    // Quad A: back face (reverse winding)
    emitQuad(
        {fx + 1.0f, fy + 0.0f, fz + 1.0f},
        {fx + 0.0f, fy + 0.0f, fz + 0.0f},
        {fx + 0.0f, fy + 1.0f, fz + 0.0f},
        {fx + 1.0f, fy + 1.0f, fz + 1.0f},
        -normalA, stateId, 3, flags, outVertices);

    // Quad B: diagonal from (+X,-Z) to (-X,+Z) — front
    emitQuad(
        {fx + 1.0f, fy + 0.0f, fz + 0.0f},
        {fx + 0.0f, fy + 0.0f, fz + 1.0f},
        {fx + 0.0f, fy + 1.0f, fz + 1.0f},
        {fx + 1.0f, fy + 1.0f, fz + 0.0f},
        normalB, stateId, 3, flags, outVertices);

    // Quad B: back face (reverse winding)
    emitQuad(
        {fx + 0.0f, fy + 0.0f, fz + 1.0f},
        {fx + 1.0f, fy + 0.0f, fz + 0.0f},
        {fx + 1.0f, fy + 1.0f, fz + 0.0f},
        {fx + 0.0f, fy + 1.0f, fz + 1.0f},
        -normalB, stateId, 3, flags, outVertices);
}

void ModelMesher::generateTorch(
    int x,
    int y,
    int z,
    const world::BlockDefinition& blockDef,
    const world::StateMap& state,
    std::vector<ModelVertex>& outVertices)
{
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    float fz = static_cast<float>(z);

    uint8_t flags = static_cast<uint8_t>((blockDef.tintIndex & 0x1) | ((blockDef.waving & 0x3) << 1));

    // Torch dimensions: 2/16 wide, 10/16 tall, centered on XZ
    constexpr float TORCH_MIN_XZ = 7.0f / 16.0f;
    constexpr float TORCH_MAX_XZ = 9.0f / 16.0f;
    constexpr float TORCH_HEIGHT = 10.0f / 16.0f;

    glm::vec3 offset(fx, fy, fz);

    // Check if wall-mounted
    auto it = state.find("facing");
    if (it != state.end() && it->second != "up")
    {
        // Wall torch: offset and tilt toward wall
        // For simplicity in V1, offset the torch toward the wall direction
        constexpr float WALL_OFFSET = 5.0f / 16.0f;
        glm::vec3 wallShift(0.0f);

        if (it->second == "north")
            wallShift.z = -WALL_OFFSET;
        else if (it->second == "south")
            wallShift.z = WALL_OFFSET;
        else if (it->second == "east")
            wallShift.x = WALL_OFFSET;
        else if (it->second == "west")
            wallShift.x = -WALL_OFFSET;

        offset += wallShift;
    }

    glm::vec3 minCorner(TORCH_MIN_XZ, 0.0f, TORCH_MIN_XZ);
    glm::vec3 maxCorner(TORCH_MAX_XZ, TORCH_HEIGHT, TORCH_MAX_XZ);

    emitBox(offset, minCorner, maxCorner, blockDef.baseStateId, 3, flags, outVertices);
}

} // namespace voxel::renderer
