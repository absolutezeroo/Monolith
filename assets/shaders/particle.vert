#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
} pc;

layout(location = 0) in vec3 inPos;       // pre-computed billboard corner position
layout(location = 1) in vec2 inUV;
layout(location = 2) in float inTexLayer;
layout(location = 3) in float inAlpha;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out flat float fragTexLayer;
layout(location = 2) out float fragAlpha;

void main() {
    gl_Position = pc.viewProjection * vec4(inPos, 1.0);
    fragUV = inUV;
    fragTexLayer = inTexLayer;
    fragAlpha = inAlpha;
}
