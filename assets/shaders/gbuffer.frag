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

// ── G-Buffer outputs (MRT) ─────────────────────────────────────────────────
layout(location = 0) out vec4 outAlbedoAO;   // RT0: albedo.rgb + AO.a
layout(location = 1) out vec2 outNormalOct;   // RT1: octahedral encoded normal.xy

// ── Octahedral normal encoding ─────────────────────────────────────────────
// Maps a unit-length 3D normal to a 2D coordinate in [0,1].
// Reference: "Survey of Efficient Representations for Independent Unit Vectors"
// (Cigolle et al. 2014)
vec2 octahedralEncode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0)
    {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return n.xy * 0.5 + 0.5; // Remap [-1,1] → [0,1]
}

void main()
{
    // Sample block texture from array using tiling UVs and texture layer
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));

    // Alpha test for cutout blocks (leaves, flowers, tall grass)
    // Opaque blocks have texColor.a == 1.0 so they always pass.
    if (texColor.a < 0.5)
        discard;

    // Apply biome tint (index 0 = white = no change)
    vec3 tint = tintPalette.colors[fragTintIndex].rgb;

    // RT0: tinted albedo + AO in alpha
    outAlbedoAO = vec4(texColor.rgb * tint, fragAO);

    // RT1: octahedral-encoded normal
    outNormalOct = octahedralEncode(normalize(fragNormal));
}
