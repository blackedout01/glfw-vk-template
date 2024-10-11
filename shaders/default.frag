// Original source in https://github.com/blackedout01/glfw-vk-template
//
// This is free and unencumbered software released into the public domain.
// Anyone is free to copy, modify, publish, use, compile, sell, or distribute
// this software, either in source code form or as a compiled binary, for any
// purpose, commercial or non-commercial, and by any means.
//
// In jurisdictions that recognize copyright laws, the author or authors of
// this software dedicate any and all copyright interest in the software to the
// public domain. We make this dedication for the benefit of the public at
// large and to the detriment of our heirs and successors. We intend this
// dedication to be an overt act of relinquishment in perpetuity of all present
// and future rights to this software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to https://unlicense.org

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