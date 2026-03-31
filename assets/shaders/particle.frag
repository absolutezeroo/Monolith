#version 450

layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in flat float fragTexLayer;
layout(location = 2) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(blockTextures, vec3(fragUV, fragTexLayer));
    float finalAlpha = texColor.a * fragAlpha;
    if (finalAlpha < 0.01) discard;
    outColor = vec4(texColor.rgb, finalAlpha);
}
