#version 450

layout(location=0) in vec3 FragColor;

layout(location=0) out vec4 Result;

void main() {
    Result = vec4(FragColor, 1.0);
}