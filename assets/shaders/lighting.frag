#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ── G-Buffer inputs ────────────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedoAO;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;

// ── Push constants ─────────────────────────────────────────────────────────
layout(push_constant) uniform LightingPushConstants {
    vec3 sunDirection;     // 12 bytes
    float ambientStrength; // 4 bytes
} pc;

// ── Octahedral normal decoding ─────────────────────────────────────────────
vec3 octahedralDecode(vec2 e)
{
    e = e * 2.0 - 1.0; // Remap [0,1] → [-1,1]
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0)
    {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

void main()
{
    // Early-out for sky pixels (depth == 1.0 means nothing was rendered)
    float depth = texture(gbufferDepth, fragUV).r;
    if (depth >= 1.0)
    {
        outColor = vec4(0.1, 0.1, 0.1, 1.0); // sky color (matches clear color)
        return;
    }

    vec4 albedoAO = texture(gbufferAlbedoAO, fragUV);
    vec2 normalEnc = texture(gbufferNormal, fragUV).rg;

    vec3 albedo = albedoAO.rgb;
    float ao = albedoAO.a;
    vec3 normal = octahedralDecode(normalEnc);

    // Soften AO: remap [0..1] → [0.4..1.0] to match forward-pass behavior
    ao = mix(0.4, 1.0, ao);

    // Directional sun lighting
    float NdotL = max(dot(normal, normalize(pc.sunDirection)), 0.0);
    vec3 diffuse = albedo * NdotL;

    // Ambient
    vec3 ambient = albedo * pc.ambientStrength;

    // Combine: (ambient + diffuse) * AO
    vec3 color = (ambient + diffuse) * ao;

    outColor = vec4(color, 1.0);
}
