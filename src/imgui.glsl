#version 450

layout(binding = 0) uniform imgui_uniform_buffer
{
    mat4 ProjectionMatrix;
};

layout(binding = 1) uniform sampler2D TextureSampler;

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
    OutColor = InColor * texture(TextureSampler, InUV);
}

#endif
