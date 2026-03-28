#version 450

// ── Gigabuffer SSBO (binding 0) ─────────────────────────────────────────────
// Packed quad data as pairs of uint32. Each quad = 8 bytes = 2 consecutive uints.
layout(std430, set = 0, binding = 0) readonly buffer Gigabuffer {
    uint data[];
} gigabuffer;

// ── ChunkRenderInfo SSBO (binding 1) ────────────────────────────────────────
// Per-chunk metadata uploaded by CPU, indexed by gl_InstanceIndex (= firstInstance from compute cull).
struct ChunkRenderInfo {
    vec4 boundingSphere;   // xyz = center, w = radius
    vec4 worldBasePos;     // xyz = section world origin, w = unused
    uint gigabufferOffset;
    uint quadCount;
    uint _pad0;
    uint _pad1;
};
layout(std430, set = 0, binding = 1) readonly buffer ChunkInfoSSBO {
    ChunkRenderInfo infos[];
} chunkInfo;

// ── Push constants ──────────────────────────────────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;    // 64 bytes (offset 0)
    float time;             // 4 bytes  (offset 64)
    float _pad0;            // 4 bytes  (offset 68)
    float _pad1;            // 4 bytes  (offset 72)
    float _pad2;            // 4 bytes  (offset 76)
} pc;

// ── Outputs to fragment shader ──────────────────────────────────────────────
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out float fragAO;
layout(location = 4) flat out uint fragBlockStateId;
layout(location = 5) flat out uint fragTintIndex;

// ── Face normals ────────────────────────────────────────────────────────────
const vec3 FACE_NORMALS[6] = vec3[6](
    vec3( 1.0,  0.0,  0.0),  // PosX
    vec3(-1.0,  0.0,  0.0),  // NegX
    vec3( 0.0,  1.0,  0.0),  // PosY
    vec3( 0.0, -1.0,  0.0),  // NegY
    vec3( 0.0,  0.0,  1.0),  // PosZ
    vec3( 0.0,  0.0, -1.0)   // NegZ
);

void main()
{
    // ── Identify which quad and which corner ────────────────────────────────
    uint quadIndex = uint(gl_VertexIndex) / 4u;
    uint cornerIndex = uint(gl_VertexIndex) % 4u;

    // ── Read packed quad data (2 consecutive uint32s) ───────────────────────
    uint lo = gigabuffer.data[quadIndex * 2u + 0u];
    uint hi = gigabuffer.data[quadIndex * 2u + 1u];

    // ── Unpack lo (bits 0-31) ───────────────────────────────────────────────
    uint posX     = bitfieldExtract(lo, 0, 6);
    uint posY     = bitfieldExtract(lo, 6, 6);
    uint posZ     = bitfieldExtract(lo, 12, 6);
    uint widthM1  = bitfieldExtract(lo, 18, 6);
    uint heightM1 = bitfieldExtract(lo, 24, 6);
    uint bsLow    = bitfieldExtract(lo, 30, 2);

    float width  = float(widthM1 + 1u);
    float height = float(heightM1 + 1u);

    // ── Unpack hi (bits 32-63) ──────────────────────────────────────────────
    uint bsHigh     = bitfieldExtract(hi, 0, 14);
    uint face       = bitfieldExtract(hi, 14, 3);
    uint ao0        = bitfieldExtract(hi, 17, 2);
    uint ao1        = bitfieldExtract(hi, 19, 2);
    uint ao2        = bitfieldExtract(hi, 21, 2);
    uint ao3        = bitfieldExtract(hi, 23, 2);
    uint flip       = bitfieldExtract(hi, 25, 1);
    uint tintIndex  = bitfieldExtract(hi, 27, 3);
    uint wavingType = bitfieldExtract(hi, 30, 2);

    uint blockStateId = bsLow | (bsHigh << 2u);

    float x = float(posX);
    float y = float(posY);
    float z = float(posZ);

    // ── Corner reconstruction per face ──────────────────────────────────────
    // 4 corners in CCW winding from outside face (world space).
    // Y-flip in projection reverses winding: world CCW → framebuffer CW.
    // Index buffer pattern {0,1,2, 2,3,0} with VK_FRONT_FACE_CLOCKWISE.
    vec3 c0, c1, c2, c3;

    if (face == 0u) // PosX (+X)
    {
        c0 = vec3(x + 1.0, y,          z);
        c1 = vec3(x + 1.0, y + height, z);
        c2 = vec3(x + 1.0, y + height, z + width);
        c3 = vec3(x + 1.0, y,          z + width);
    }
    else if (face == 1u) // NegX (-X)
    {
        c0 = vec3(x, y,          z + width);
        c1 = vec3(x, y + height, z + width);
        c2 = vec3(x, y + height, z);
        c3 = vec3(x, y,          z);
    }
    else if (face == 2u) // PosY (+Y, up)
    {
        // Mesher: width = Z extent, height = X extent for Y-axis faces.
        c0 = vec3(x,          y + 1.0, z);
        c1 = vec3(x,          y + 1.0, z + width);
        c2 = vec3(x + height, y + 1.0, z + width);
        c3 = vec3(x + height, y + 1.0, z);
    }
    else if (face == 3u) // NegY (-Y, down)
    {
        // Mesher: width = Z extent, height = X extent for Y-axis faces.
        c0 = vec3(x,          y, z);
        c1 = vec3(x + height, y, z);
        c2 = vec3(x + height, y, z + width);
        c3 = vec3(x,          y, z + width);
    }
    else if (face == 4u) // PosZ (+Z)
    {
        c0 = vec3(x + width, y,          z + 1.0);
        c1 = vec3(x + width, y + height, z + 1.0);
        c2 = vec3(x,         y + height, z + 1.0);
        c3 = vec3(x,         y,          z + 1.0);
    }
    else // face == 5u, NegZ (-Z)
    {
        c0 = vec3(x,         y,          z);
        c1 = vec3(x,         y + height, z);
        c2 = vec3(x + width, y + height, z);
        c3 = vec3(x + width, y,          z);
    }

    // ── Quad diagonal flip ──────────────────────────────────────────────────
    // When flip == 1, rotate corners by one position to change the triangle
    // diagonal from c0-c2 to c1-c3 for AO-correct interpolation.
    // A swap of c1↔c3 would reverse winding and cause back-face culling;
    // a rotation [c0,c1,c2,c3] → [c1,c2,c3,c0] preserves winding order.
    if (flip == 1u)
    {
        vec3 temp = c0;
        c0 = c1;
        c1 = c2;
        c2 = c3;
        c3 = temp;
    }

    // ── AO per corner ───────────────────────────────────────────────────────
    // Raw AO: 3 = no occlusion → 1.0 brightness, 0 = full occlusion → 0.0
    float aoValues[4] = float[4](
        float(ao0) / 3.0,
        float(ao1) / 3.0,
        float(ao2) / 3.0,
        float(ao3) / 3.0
    );

    // When flipped, rotate AO values to match the corner rotation
    if (flip == 1u)
    {
        float temp = aoValues[0];
        aoValues[0] = aoValues[1];
        aoValues[1] = aoValues[2];
        aoValues[2] = aoValues[3];
        aoValues[3] = temp;
    }

    // ── UV coordinates per corner ───────────────────────────────────────────
    vec2 uvs[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(0.0, height),
        vec2(width, height),
        vec2(width, 0.0)
    );

    // When flipped, rotate UVs to match the corner rotation
    if (flip == 1u)
    {
        vec2 temp = uvs[0];
        uvs[0] = uvs[1];
        uvs[1] = uvs[2];
        uvs[2] = uvs[3];
        uvs[3] = temp;
    }

    // ── Select corner ───────────────────────────────────────────────────────
    vec3 localPos;
    if (cornerIndex == 0u) localPos = c0;
    else if (cornerIndex == 1u) localPos = c1;
    else if (cornerIndex == 2u) localPos = c2;
    else localPos = c3;

    fragAO = aoValues[cornerIndex];
    fragUV = uvs[cornerIndex];

    // ── World position ──────────────────────────────────────────────────────
    vec3 chunkWorldPos = chunkInfo.infos[gl_InstanceIndex].worldBasePos.xyz;
    vec3 worldPos = chunkWorldPos + localPos;

    // ── Waving vertex animation ─────────────────────────────────────────────
    if (wavingType == 1u) // Leaves: slow XZ sway
    {
        float phase = dot(worldPos.xz, vec2(0.7, 0.3)) + pc.time * 1.5;
        worldPos.x += sin(phase) * 0.04;
        worldPos.z += cos(phase * 1.3) * 0.04;
    }
    else if (wavingType == 2u) // Plants: faster Y+XZ bob, anchored at base
    {
        float localY = fract(localPos.y);
        float phase = dot(worldPos.xz, vec2(0.5, 0.5)) + pc.time * 2.0;
        worldPos.x += sin(phase) * 0.08 * localY;
        worldPos.y += sin(phase * 0.7) * 0.02 * localY;
        worldPos.z += cos(phase * 1.1) * 0.06 * localY;
    }
    else if (wavingType == 3u) // Liquid: surface wave
    {
        float phase = worldPos.x * 0.5 + worldPos.z * 0.3 + pc.time * 1.0;
        worldPos.y += sin(phase) * 0.03;
    }

    // ── Output ──────────────────────────────────────────────────────────────
    fragWorldPos = worldPos;
    fragNormal = FACE_NORMALS[face];
    fragBlockStateId = blockStateId;
    fragTintIndex = tintIndex;

    gl_Position = pc.viewProjection * vec4(worldPos, 1.0);
}
