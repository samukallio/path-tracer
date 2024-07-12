const uint BASIC_METAL_BASE_SPECTRUM                      = 1;
const uint BASIC_METAL_SPECULAR_SPECTRUM                  = 5;
const uint BASIC_METAL_ROUGHNESS                          = 9;
const uint BASIC_METAL_ROUGHNESS_TEXTURE_INDEX            = 10;
const uint BASIC_METAL_ROUGHNESS_ANISOTROPY               = 11;
const uint BASIC_METAL_ROUGHNESS_ANISOTROPY_TEXTURE_INDEX = 12;

struct basic_metal_parameters
{
    vec4  Lambda;                 // Sampling wavelengths.
    vec4  BaseReflectance;        // Base reflectance value at each of the 4 sampling wavelengths.
    vec4  SpecularReflectance;    // 82-degree reflectance of the metal base substrate at each of the 4 sampling wavelengths.
    vec2  RoughnessAlpha;         // GGX roughness alpha (X and Y).
};

vec3 BasicMetal_BaseColor(uint MaterialIndex, vec2 TextureUV)
{
    return MaterialTexturableColor(MaterialIndex, BASIC_METAL_BASE_SPECTRUM, TextureUV);
}

basic_metal_parameters BasicMetal_Parameters
(
    uint MaterialIndex,
    vec2 TextureUV,
    vec4 Lambda,
    vec4 ExteriorIOR
)
{
    basic_metal_parameters Parameters;

    Parameters.Lambda = Lambda;

    Parameters.BaseReflectance = MaterialTexturableReflectance
    (
        MaterialIndex,
        BASIC_METAL_BASE_SPECTRUM,
        Lambda, TextureUV
    );

    Parameters.SpecularReflectance = MaterialTexturableReflectance
    (
        MaterialIndex,
        BASIC_METAL_SPECULAR_SPECTRUM,
        Lambda, TextureUV
    );

    Parameters.RoughnessAlpha = GGXRoughnessAlpha
    (
        MaterialTexturableValue
        (
            MaterialIndex,
            BASIC_METAL_ROUGHNESS,
            TextureUV
        ),
        MaterialTexturableValue
        (
            MaterialIndex,
            BASIC_METAL_ROUGHNESS_ANISOTROPY,
            TextureUV
        )
    );

    return Parameters;
}

void BasicMetal_Sample
(
    basic_metal_parameters Parameters,
    vec3 Out, out vec3 In,
    inout vec4 PathThroughput,
    inout vec4 PathDensity
)
{
    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(Out * sign(Out.z), Parameters.RoughnessAlpha, NormalU1, NormalU2);
    float Cosine = clamp(dot(Normal, Out), -1.0, +1.0);

    // Compute reflected direction.
    In = 2 * Cosine * Normal - Out;

    // If the reflected direction is in the wrong hemisphere,
    // then it is shadowed and we terminate here.
    if (Out.z * In.z <= 0)
    {
        PathDensity = vec4(0.0);
        return;
    }

    float Shadowing = GGXSmithG1(Out, Parameters.RoughnessAlpha);

    vec4 Fresnel = SchlickFresnelMetal
    (
        Parameters.BaseReflectance,
        Parameters.SpecularReflectance,
        abs(Cosine)
    );

    PathThroughput *= Fresnel * Shadowing;
}
