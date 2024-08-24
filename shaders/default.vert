#version 450

layout(location=0) in vec3 VertPosition;
layout(location=1) in vec3 VertColor;
layout(location=2) in vec2 VertTexCoord;

layout(location=0) out vec3 FragColor;
layout(location=1) out vec2 FragTexCoord;

layout(set=0, binding=0) uniform UniformBufferObject {
    mat4 M;
    mat4 V;
    mat4 P;
} Mats;

layout(push_constant) uniform PushConstant {
    mat3 M;
    mat3 N;
    vec3 T;
};

void main() {
    gl_Position = Mats.P*Mats.V*Mats.M*vec4(VertPosition, 1.0);
    FragColor = VertColor;
    FragTexCoord = VertTexCoord;
}