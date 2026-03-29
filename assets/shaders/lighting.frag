#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ── G-Buffer inputs ────────────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedoAO;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;
layout(set = 0, binding = 3) uniform sampler2D gbufferLight;

// ── Push constants ─────────────────────────────────────────────────────────
layout(push_constant) uniform LightingPushConstants {
    vec3 sunDirection;     // 12 bytes
    float ambientStrength; // 4 bytes
    float dayNightFactor;  // 4 bytes
    float timeOfDay;       // 4 bytes
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
    float depth = texture(gbufferDepth, fragUV).r;

    // Sky pixels (depth == 1.0 means nothing was rendered)
    if (depth >= 1.0)
    {
        // Sky color gradient based on time of day
        vec3 daySky = vec3(0.4, 0.6, 0.9);
        vec3 nightSky = vec3(0.01, 0.01, 0.03);
        vec3 sunsetSky = vec3(0.9, 0.5, 0.2);

        float sunsetFactor = smoothstep(0.3, 0.5, pc.dayNightFactor) * smoothstep(0.8, 0.6, pc.dayNightFactor);
        vec3 skyColor = mix(nightSky, daySky, pc.dayNightFactor);
        skyColor = mix(skyColor, sunsetSky, sunsetFactor * 0.5);

        outColor = vec4(skyColor, 1.0);
        return;
    }

    vec4 albedoAO = texture(gbufferAlbedoAO, fragUV);
    vec2 normalEnc = texture(gbufferNormal, fragUV).rg;
    vec2 lightValues = texture(gbufferLight, fragUV).rg;

    vec3 albedo = albedoAO.rgb;
    float ao = albedoAO.a;
    vec3 normal = octahedralDecode(normalEnc);
    float skyLight = lightValues.r;
    float blockLight = lightValues.g;

    // Soften AO: remap [0..1] → [0.4..1.0] to match forward-pass behavior
    ao = mix(0.4, 1.0, ao);

    // Sky contribution: modulated by day/night cycle
    vec3 skyContribution = skyLight * pc.dayNightFactor * vec3(0.95, 0.95, 1.0);

    // Block contribution: warm orange, always full strength
    vec3 blockContribution = blockLight * vec3(1.0, 0.85, 0.7);

    // Take max (not add) — matches Minecraft behavior
    vec3 lightLevel = max(skyContribution, blockContribution);

    // Minimum ambient so caves aren't pitch black
    lightLevel = max(lightLevel, vec3(pc.ambientStrength));

    // Directional sun as subtle bonus (zero at night via dayNightFactor)
    float NdotL = max(dot(normal, normalize(pc.sunDirection)), 0.0);
    vec3 sunBonus = albedo * NdotL * 0.15 * pc.dayNightFactor;

    // Combine: (albedo * voxel light + sun bonus) * AO
    vec3 color = (albedo * lightLevel + sunBonus) * ao;

    outColor = vec4(color, 1.0);
}
