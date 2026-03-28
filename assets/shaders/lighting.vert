#version 450

layout(location = 0) out vec2 fragUV;

void main()
{
    // Fullscreen triangle: 3 vertices, no VBO needed.
    // Generates a triangle from (-1,-1) to (3,-1) to (-1,3) covering the viewport.
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
}
