#version 450

// ── Inputs from vertex shader ───────────────────────────────────────────────
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in float fragAO;
layout(location = 4) flat in uint fragBlockStateId;
layout(location = 5) flat in uint fragTintIndex;

// ── Output ──────────────────────────────────────────────────────────────────
layout(location = 0) out vec4 outColor;

void main()
{
    // DEBUG: color based on world position fractional (repeats every 16 blocks)
    // If geometry is correct, you'll see a colorful rainbow pattern across terrain.
    // If all quads overlap, you'll see a single solid color per section.
    vec3 debugColor = fract(fragWorldPos / 16.0);
    outColor = vec4(debugColor, 1.0);
}
