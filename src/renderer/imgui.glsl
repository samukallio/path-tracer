#version 450

#define BIND_SCENE 1    // Scene data in descriptor set 0.

#include "core/common.glsl.inc"
#include "scene/scene.glsl.inc"

layout(set=0, binding=0)
uniform sampler2D TextureSampler;

layout(push_constant)
uniform ImGuiPushConstants
{
    mat4 ProjectionMatrix;
    uint TextureID;
};

#if VERTEX

layout(location = 0) in vec2 InPosition;
layout(location = 1) in vec2 InUV;
layout(location = 2) in vec4 InColor;

layout(location = 0) out vec2 OutUV;
layout(location = 1) out vec4 OutColor;

void main()
{
    gl_Position = ProjectionMatrix * vec4(InPosition, 0, 1);
    OutUV = InUV;
    OutColor = InColor;
}

#elif FRAGMENT

layout(location = 0) in vec2 InUV;
layout(location = 1) in vec4 InColor;

layout(location = 0) out vec4 OutColor;

void main()
{
    if (TextureID > 0)
        OutColor = InColor * SampleTexture(TextureID - 1, InUV);
    else
        OutColor = InColor * texture(TextureSampler, InUV);
}

#endif
