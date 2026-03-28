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

// ── Output ──────────────────────────────────────────────────────────────────
layout(location = 0) out vec4 outColor;

void main()
{
    // Sample block texture from array using tiling UVs and texture layer
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));

    // Apply ambient occlusion darkening
    vec3 color = texColor.rgb * fragAO;

    outColor = vec4(color, texColor.a);
}
