#version 450

layout(location=0) in vec2 VertPosition;
layout(location=1) in vec3 VertColor;

layout(location=0) out vec3 FragColor;

#if 0
vec2 Positions[12] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5),
    vec2(0.0, -0.7),
    vec2(0.5, 0.7),
    vec2(-0.5, 0.7),
    vec2(-1.0, -0.7),
    vec2(0.5, 0.7),
    vec2(-0.5, 0.7),
    vec2(-1.0, -0.7),
    vec2(0.5, 0.7),
    vec2(-1.5, 0.7)
);

vec3 Colors[12] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 0.0, 1.0),
    vec3(0.0, 1.0, 0.0),
    vec3(1.0, 0.0, 1.0),
    vec3(1.0, 1.0, 1.0),
    vec3(0.0, 1.0, 0.0),
    vec3(1.0, 1.0, 1.0)
);
#endif

void main() {
    gl_Position = vec4(VertPosition, 0.0, 1.0);
    FragColor = VertColor;
}