#version 450

layout(binding = 0) uniform sampler2D textureSampler;

#if VERTEX

vec2 positions[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

vec2 uvs[6] = vec2[](
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 1.0)
);

layout(location = 0) out vec2 fragmentUV;

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragmentUV = uvs[gl_VertexIndex];
}

#elif FRAGMENT

layout(location = 0) in vec2 fragmentUV;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 value = texture(textureSampler, fragmentUV);

    if (value.a > 0)
        outColor = vec4(value.rgb / value.a, 1);
    else
        outColor = vec4(0, 0, 0, 1);
}

#endif
