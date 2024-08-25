#version 450

layout(location=0) in vec3 FragColor;
layout(location=1) in vec2 FragTexCoord;

layout(location=0) out vec4 Result;

layout(set=1, binding=1) uniform sampler Sampler;
layout(set=1, binding=2) uniform texture2D Tex;

void main() {
    vec4 TexColor = texture(sampler2D(Tex, Sampler), FragTexCoord);
    Result = vec4(FragColor*TexColor.rgb, 1.0);
}