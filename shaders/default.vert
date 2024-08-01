#version 450

layout(location=0) in vec3 VertPosition;
layout(location=1) in vec3 VertColor;

layout(location=0) out vec3 FragColor;

layout(binding=0) uniform UniformBufferObject {
    mat4 M;
    mat4 V;
    mat4 P;
} Mats;

void main() {
    gl_Position = Mats.P*Mats.V*Mats.M*vec4(VertPosition, 1.0);
    FragColor = VertColor;
}