#version 450

layout(location=0) in vec3 VertPosition;
layout(location=1) in vec3 VertNormal;
layout(location=2) in vec2 VertTexCoord;

layout(location=0) out vec3 FragNormal;
layout(location=1) out vec2 FragTexCoord;

layout(set=0, binding=0) uniform UniformBuffer1 {
    mat4 V;
    mat4 P;
    vec4 L;
};

layout(push_constant) uniform PushConstants {
    mat4 M;
    mat2 TexM;
    vec2 TexT;
};

void main() {
    gl_Position = P*V*M*vec4(VertPosition, 1.0);
    FragNormal = VertNormal;
    FragTexCoord = VertTexCoord;
}