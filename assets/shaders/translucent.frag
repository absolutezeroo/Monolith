#version 450

// ── Inputs from vertex shader ───────────────────────────────────────────────
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in float fragAO;
layout(location = 4) flat in uint fragTextureLayer;
layout(location = 5) flat in uint fragTintIndex;

// ── Block texture array (binding 4) ────────────────────────────────────────
layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;

// ── Push constants (shared with chunk.vert) ─────────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;    // 64 bytes (offset 0)
    float time;             // 4 bytes  (offset 64)
    float ambientStrength;  // 4 bytes  (offset 68)
    float _pad0;            // 4 bytes  (offset 72)
    float _pad1;            // 4 bytes  (offset 76)
    vec4 sunDirection;      // 16 bytes (offset 80, w unused)
} pc;

// ── Output to swapchain (single color attachment, alpha-blended) ───────────
layout(location = 0) out vec4 outColor;

void main()
{
    // Sample block texture from array
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));

    // Discard fully transparent fragments
    if (texColor.a < 0.01)
        discard;

    // Directional + ambient lighting (matches deferred lighting pass via shared push constants)
    vec3 normal = normalize(fragNormal);
    float NdotL = max(dot(normal, pc.sunDirection.xyz), 0.0);
    float lighting = pc.ambientStrength + (1.0 - pc.ambientStrength) * NdotL;

    // Apply AO
    lighting *= fragAO;

    outColor = vec4(texColor.rgb * lighting, texColor.a);
}
