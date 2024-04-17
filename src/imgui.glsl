#version 450

layout(binding = 0) uniform ImGuiUniformBuffer
{
    mat4 projectionMatrix;
};

layout(binding = 1) uniform sampler2D textureSampler;

#if VERTEX

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main()
{
    gl_Position = projectionMatrix * vec4(inPosition, 0, 1);
    outUV = inUV;
    outColor = inColor;
}

#elif FRAGMENT

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = inColor * texture(textureSampler, inUV);
}

#endif
