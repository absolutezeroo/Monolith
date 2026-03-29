#version 450

// ── Gigabuffer SSBO (binding 0) ─────────────────────────────────────────────
// ModelVertex data stored as raw uint32s. Each ModelVertex = 40 bytes = 10 uint32s.
layout(std430, set = 0, binding = 0) readonly buffer Gigabuffer {
    uint data[];
} gigabuffer;

// ── ChunkRenderInfo SSBO (binding 1) ────────────────────────────────────────
struct ChunkRenderInfo {
    vec4 boundingSphere;       // xyz = center, w = radius
    vec4 worldBasePos;         // xyz = section world origin, w = unused
    uint gigabufferOffset;     // opaque
    uint quadCount;            // opaque
    uint transGigabufferOffset;// translucent
    uint transQuadCount;       // translucent
    uint modelGigabufferOffset;// model vertex byte offset
    uint modelVertexCount;     // model vertex count
    uint _pad0, _pad1;         // pad to 64 bytes
};
layout(std430, set = 0, binding = 1) readonly buffer ChunkInfoSSBO {
    ChunkRenderInfo infos[];
} chunkInfo;

// ── Push constants ──────────────────────────────────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;    // 64 bytes (offset 0)
    float time;             // 4 bytes  (offset 64)
    float ambientStrength;  // 4 bytes  (offset 68)
    float dayNightFactor;   // 4 bytes  (offset 72)
    float _pad1;            // 4 bytes  (offset 76)
    vec4 sunDirection;      // 16 bytes (offset 80)
} pc;

// ── Outputs to fragment shader (same as chunk.vert) ────────────────────────
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out float fragAO;
layout(location = 4) flat out uint fragTextureLayer;
layout(location = 5) flat out uint fragTintIndex;
layout(location = 6) out float fragSkyLight;
layout(location = 7) out float fragBlockLight;

void main()
{
    // ── Read ModelVertex from Gigabuffer (10 consecutive uint32s) ───────────
    uint modelOffset = chunkInfo.infos[gl_InstanceIndex].modelGigabufferOffset / 4u;
    uint base = modelOffset + uint(gl_VertexIndex) * 10u;

    // Position (3 floats)
    vec3 position = vec3(
        uintBitsToFloat(gigabuffer.data[base + 0u]),
        uintBitsToFloat(gigabuffer.data[base + 1u]),
        uintBitsToFloat(gigabuffer.data[base + 2u])
    );

    // Normal (3 floats)
    vec3 normal = vec3(
        uintBitsToFloat(gigabuffer.data[base + 3u]),
        uintBitsToFloat(gigabuffer.data[base + 4u]),
        uintBitsToFloat(gigabuffer.data[base + 5u])
    );

    // UV (2 floats)
    vec2 uv = vec2(
        uintBitsToFloat(gigabuffer.data[base + 6u]),
        uintBitsToFloat(gigabuffer.data[base + 7u])
    );

    // Side faces: flip V so texture-top maps to world-top (high Y), matching chunk.vert.
    // In Vulkan, V=0 is the top of the image as loaded by stb_image.
    // ModelMesher emits side-face vertices with Y increasing from v0→v3 and V from 0→1,
    // so without the flip V=0 (texture top) maps to world-bottom → upside-down textures.
    if (abs(normal.y) < 0.5) {
        uv.y = 1.0 - uv.y;
    }

    // Packed field: blockStateId(16) | ao(8) | flags(8)
    uint packed0 = gigabuffer.data[base + 8u];
    uint textureLayer = packed0 & 0xFFFFu;
    uint ao = (packed0 >> 16u) & 0xFFu;
    uint flags = (packed0 >> 24u) & 0xFFu;

    uint tintIndex = flags & 0x1u;
    uint wavingType = (flags >> 1u) & 0x3u;

    // Packed field: light(8) | pad(24)
    uint packed1 = gigabuffer.data[base + 9u];
    uint lightByte = packed1 & 0xFFu;
    float skyLight = float((lightByte >> 4u) & 0xFu) / 15.0;
    float blockLight = float(lightByte & 0xFu) / 15.0;

    // ── World position ─────────────────────────────────────────────────────
    vec3 chunkWorldPos = chunkInfo.infos[gl_InstanceIndex].worldBasePos.xyz;
    vec3 worldPos = chunkWorldPos + position;

    // ── Waving vertex animation (same as chunk.vert) ────────────────────────
    if (wavingType == 1u) // Leaves
    {
        float phase = dot(worldPos.xz, vec2(0.7, 0.3)) + pc.time * 1.5;
        worldPos.x += sin(phase) * 0.04;
        worldPos.z += cos(phase * 1.3) * 0.04;
    }
    else if (wavingType == 2u) // Plants
    {
        float localY = fract(position.y);
        float phase = dot(worldPos.xz, vec2(0.5, 0.5)) + pc.time * 2.0;
        worldPos.x += sin(phase) * 0.08 * localY;
        worldPos.y += sin(phase * 0.7) * 0.02 * localY;
        worldPos.z += cos(phase * 1.1) * 0.06 * localY;
    }
    else if (wavingType == 3u) // Liquid
    {
        float phase = worldPos.x * 0.5 + worldPos.z * 0.3 + pc.time * 1.0;
        worldPos.y += sin(phase) * 0.03;
    }

    // ── Output ─────────────────────────────────────────────────────────────
    fragWorldPos = worldPos;
    fragNormal = normal;
    fragUV = uv;
    fragAO = float(ao) / 3.0;
    fragTextureLayer = textureLayer;
    fragTintIndex = tintIndex;
    fragSkyLight = skyLight;
    fragBlockLight = blockLight;

    gl_Position = pc.viewProjection * vec4(worldPos, 1.0);
}
