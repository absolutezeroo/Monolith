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

// ── Push constants (same as opaque pass) ───────────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;    // 64 bytes (offset 0)
    float time;             // 4 bytes  (offset 64)
    float _pad0;
    float _pad1;
    float _pad2;
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

    // Simple directional + ambient lighting (matches deferred lighting pass)
    vec3 sunDirection = normalize(vec3(0.3, 1.0, 0.5));
    float ambientStrength = 0.3;
    vec3 normal = normalize(fragNormal);
    float NdotL = max(dot(normal, sunDirection), 0.0);
    float lighting = ambientStrength + (1.0 - ambientStrength) * NdotL;

    // Apply AO
    lighting *= fragAO;

    outColor = vec4(texColor.rgb * lighting, texColor.a);
}
