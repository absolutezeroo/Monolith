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
    // Placeholder face-normal-based coloring (textures come in Story 6.5)
    vec3 color;

    if (fragNormal.y > 0.5)       // PosY (top) → green
    {
        color = vec3(0.3, 0.8, 0.2);
    }
    else if (fragNormal.y < -0.5) // NegY (bottom) → brown
    {
        color = vec3(0.5, 0.3, 0.1);
    }
    else                           // Sides → gray
    {
        color = vec3(0.6, 0.6, 0.6);
    }

    // Apply ambient occlusion darkening
    color *= fragAO;

    outColor = vec4(color, 1.0);
}
