#version 450

// ── Inputs from vertex shader ───────────────────────────────────────────────
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in float fragAO;
layout(location = 4) flat in uint fragTextureLayer;
layout(location = 5) flat in uint fragTintIndex;
layout(location = 6) in float fragSkyLight;   // Wired but unused until 8.2
layout(location = 7) in float fragBlockLight;  // Wired but unused until 8.2

// ── Block texture array (binding 4) ────────────────────────────────────────
layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;

// ── Tint palette SSBO (binding 5) ─────────────────────────────────────────
layout(std430, set = 0, binding = 5) readonly buffer TintPaletteSSBO {
    vec4 colors[8];
} tintPalette;

// ── Output ──────────────────────────────────────────────────────────────────
layout(location = 0) out vec4 outColor;

void main()
{
    // Sample block texture from array using tiling UVs and texture layer
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));

    // Apply biome tint (index 0 = white = no change)
    vec3 tint = tintPalette.colors[fragTintIndex].rgb;

    // Soften ambient occlusion: remap [0..1] → [0.4..1.0] so darkest corner
    // is 40% brightness instead of black. Reduces harsh diagonal AO artifacts.
    float ao = mix(0.4, 1.0, fragAO);
    vec3 color = texColor.rgb * tint * ao;

    outColor = vec4(color, texColor.a);
}
