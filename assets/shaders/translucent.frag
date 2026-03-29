#version 450

// ── Inputs from vertex shader ───────────────────────────────────────────────
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in float fragAO;
layout(location = 4) flat in uint fragTextureLayer;
layout(location = 5) flat in uint fragTintIndex;
layout(location = 6) in float fragSkyLight;
layout(location = 7) in float fragBlockLight;

// ── Block texture array (binding 4) ────────────────────────────────────────
layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;

// ── Tint palette SSBO (binding 5) ─────────────────────────────────────────
layout(std430, set = 0, binding = 5) readonly buffer TintPaletteSSBO {
    vec4 colors[8];
} tintPalette;

// ── Push constants (shared with chunk.vert) ─────────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;    // 64 bytes (offset 0)
    float time;             // 4 bytes  (offset 64)
    float ambientStrength;  // 4 bytes  (offset 68)
    float dayNightFactor;   // 4 bytes  (offset 72)
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

    // Apply biome tint (index 0 = white = no change)
    vec3 tint = tintPalette.colors[fragTintIndex].rgb;
    vec3 tintedColor = texColor.rgb * tint;

    // Voxel-based lighting (matches deferred lighting pass formula)
    // Sky contribution: modulated by day/night cycle
    vec3 skyContribution = fragSkyLight * pc.dayNightFactor * vec3(0.95, 0.95, 1.0);

    // Block contribution: warm orange, always full strength
    vec3 blockContribution = fragBlockLight * vec3(1.0, 0.85, 0.7);

    // Take max (not add) — matches Minecraft behavior
    vec3 lightLevel = max(skyContribution, blockContribution);

    // Minimum ambient so caves aren't pitch black
    lightLevel = max(lightLevel, vec3(pc.ambientStrength));

    // Directional sun as subtle bonus
    vec3 normal = normalize(fragNormal);
    float NdotL = max(dot(normal, pc.sunDirection.xyz), 0.0);
    vec3 sunBonus = tintedColor * NdotL * 0.15 * pc.dayNightFactor;

    // Apply AO
    float ao = mix(0.4, 1.0, fragAO);

    // Combine
    vec3 color = (tintedColor * lightLevel + sunBonus) * ao;

    outColor = vec4(color, texColor.a);
}
