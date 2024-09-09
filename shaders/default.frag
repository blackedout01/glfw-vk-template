#version 450

layout(location=0) in vec3 FragNormal;
layout(location=1) in vec2 FragTexCoord;

layout(location=0) out vec4 Result;

layout(set=1, binding=1) uniform sampler Sampler;
layout(set=1, binding=2) uniform texture2D Tex;

layout(set=0, binding=0) uniform UniformBuffer1 {
    mat4 V;
    mat4 P;
    vec4 L;
};

layout(push_constant, std430) uniform PushConstants {
    mat4 M;
    mat2 TexM;
    vec2 TexT;
};

void main() {
    vec3 LightDir = normalize(-L.xyz);

    float Ambient = 0.3;
    float Diffuse = max(dot(normalize(FragNormal), LightDir), 0.0);
    float I = Ambient + (1.0 - Ambient)*Diffuse;

    vec4 TexColor = texture(sampler2D(Tex, Sampler), TexM*FragTexCoord + TexT);
    Result = vec4(I*TexColor.rgb, 1.0);
    //Result = vec4(vec3(TexT.st, 0.0), 1.0);
}