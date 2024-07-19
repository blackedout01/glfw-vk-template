#version 450

layout(location=0) in vec2 VertPosition;
layout(location=1) in vec3 VertColor;

layout(location=0) out vec3 FragColor;

layout(binding=0) uniform UniformBufferObject {
    mat4 M;
    mat4 V;
    mat4 P;
    vec2 A;
} Mat;

void main() {
    // Mat.M*Mat.V*Mat.P*
    gl_Position = vec4(VertPosition + Mat.A, 0.0, 1.0);
    FragColor = VertColor;
}