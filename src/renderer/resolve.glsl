// Tone mapping code taken from https://64.github.io/tonemapping/

#version 450

#include "core/common.glsl.inc"

layout(set=0, binding=0)
uniform sampler2D SampleAccumulatorImageSampler;

layout(push_constant)
uniform ResolvePushConstants
{
    float   Brightness;
    uint    ToneMappingMode;
    float   ToneMappingWhiteLevel;
};

#if VERTEX

vec2 Positions[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

vec2 UVs[6] = vec2[](
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 1.0)
);

layout(location = 0) out vec2 FragmentUV;

void main()
{
    gl_Position = vec4(Positions[gl_VertexIndex], 0.0, 1.0);
    FragmentUV = UVs[gl_VertexIndex];
}

#elif FRAGMENT

layout(location = 0) in vec2 FragmentUV;

layout(location = 0) out vec4 OutColor;

float Luminance(vec3 Color)
{
    return dot(Color, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec3 ToneMapReinhard(vec3 Color)
{
    float OldL = Luminance(Color);
    float MaxL = ToneMappingWhiteLevel;
    float N = OldL * (1.0f + (OldL / (MaxL * MaxL)));
    float NewL = N / (1.0f + OldL);
    return Color * NewL / OldL;
}

vec3 ToneMapHablePartial(vec3 X)
{
    float A = 0.15f, B = 0.50f, C = 0.10f;
    float D = 0.20f, E = 0.02f, F = 0.30f;
    return ((X * (A * X + C * B) + D * E) / (X * (A * X + B) + D * F)) - E / F;
}

vec3 ToneMapHable(vec3 Color)
{
    float ExposureBias = 2.0f;
    vec3 Current = ToneMapHablePartial(Color * ExposureBias);
    vec3 W = vec3(11.2f);
    vec3 WhiteScale = vec3(1.0f) / ToneMapHablePartial(W);
    return Current * WhiteScale;
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

vec3 ToneMapACES(vec3 Color)
{
    vec3 V = ACES_INPUT_MATRIX * Color;
    vec3 A = V * (V + 0.0245786f) - 0.000090537f;
    vec3 B = V * (0.983729f * V + 0.4329510f) + 0.238081f;
    return ACES_OUTPUT_MATRIX * (A / B);
}

void main()
{
    vec3 Color = vec3(0, 0, 0);

    vec4 Value = texture(SampleAccumulatorImageSampler, FragmentUV);
    if (Value.a > 0)
        Color = Brightness * Value.rgb / Value.a;

    if (ToneMappingMode == TONE_MAPPING_MODE_CLAMP)
        Color = clamp(Color, 0, 1);
    if (ToneMappingMode == TONE_MAPPING_MODE_REINHARD)
        Color = ToneMapReinhard(Color);
    if (ToneMappingMode == TONE_MAPPING_MODE_HABLE)
        Color = ToneMapHable(Color);
    if (ToneMappingMode == TONE_MAPPING_MODE_ACES)
        Color = ToneMapACES(Color);

    OutColor = vec4(Color, 1);
}

#endif
