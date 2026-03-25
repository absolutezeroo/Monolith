#version 450

// Vulkan NDC: X right, Y down, Z [0,1]
const vec2 positions[3] = vec2[3](
    vec2( 0.0, -0.5),   // top center
    vec2( 0.5,  0.5),   // bottom right
    vec2(-0.5,  0.5)    // bottom left
);

const vec3 colors[3] = vec3[3](
    vec3(1.0, 0.0, 0.0),  // red
    vec3(0.0, 1.0, 0.0),  // green
    vec3(0.0, 0.0, 1.0)   // blue
);

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor   = colors[gl_VertexIndex];
}
