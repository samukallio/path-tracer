#version 450

#include "common.glsl.inc"

layout(binding = 1) uniform sampler2D textureSampler;

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

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec3 ToneMapReinhard(vec3 color)
{
    float oldL = Luminance(color);
    float maxL = toneMapping.whiteLevel;
    float n = oldL * (1.0f + (oldL / (maxL * maxL)));
    float newL = n / (1.0f + oldL);
    return color * newL / oldL;
}

vec3 ToneMapHablePartial(vec3 x)
{
    float a = 0.15f, b = 0.50f, c = 0.10f;
    float d = 0.20f, e = 0.02f, f = 0.30f;
    return ((x*(a*x+c*b)+d*e)/(x*(a*x+b)+d*f))-e/f;
}

vec3 ToneMapHable(vec3 color)
{
    float exposureBias = 2.0f;
    vec3 current = ToneMapHablePartial(color * exposureBias);
    vec3 w = vec3(11.2f);
    vec3 whiteScale = vec3(1.0f) / ToneMapHablePartial(w);
    return current * whiteScale;
}

const mat3 ACES_INPUT_MATRIX = mat3(
     0.59719f,  0.07600f,  0.02840f,
     0.35458f,  0.90834f,  0.13383f,
     0.04823f,  0.01566f,  0.83777f
);

const mat3 ACES_OUTPUT_MATRIX = mat3(
     1.60475f, -0.10208f, -0.00327f,
    -0.53108f,  1.10813f, -0.07276f,
    -0.07367f, -0.00605f,  1.07602f
);

vec3 ToneMapACES(vec3 color)
{
    vec3 v = ACES_INPUT_MATRIX * color;
    vec3 a = v * (v + 0.0245786f) - 0.000090537f;
    vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return ACES_OUTPUT_MATRIX * (a / b);
}

void main()
{
    vec3 color = vec3(0, 0, 0);

    vec4 value = texture(textureSampler, fragmentUV);
    if (value.a > 0)
        color = value.rgb / value.a;

    if (toneMapping.mode == TONE_MAPPING_MODE_CLAMP)
        color = clamp(color, 0, 1);
    if (toneMapping.mode == TONE_MAPPING_MODE_REINHARD)
        color = ToneMapReinhard(color);
    if (toneMapping.mode == TONE_MAPPING_MODE_HABLE)
        color = ToneMapHable(color);
    if (toneMapping.mode == TONE_MAPPING_MODE_ACES)
        color = ToneMapACES(color);

    outColor = vec4(color, 1);
}

#endif
