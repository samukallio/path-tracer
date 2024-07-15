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

bool BasicMetal_Sample
(
    in  basic_metal_parameters Parameters,
    in  vec3 Out,
    out vec3 In,
    out vec4 Throughput,
    out vec4 InPDF
)
{
    if (Out.z <= 0.0) return false;

    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(Out, Parameters.RoughnessAlpha, NormalU1, NormalU2);
    float OutDotNormal = min(dot(Normal, Out), 1.0);

    // Compute reflected direction.
    In = 2 * OutDotNormal * Normal - Out;

    // If the reflected direction is in the wrong hemisphere,
    // then it is shadowed and we terminate here.
    if (In.z <= 0.0) return false;

    vec4 F = SchlickFresnelMetal
    (
        Parameters.BaseReflectance,
        Parameters.SpecularReflectance,
        abs(OutDotNormal)
    );

    // NormalPDF = Gm * OutDotNormal * D / Out.z
    // InPDF = NormalPDF / (4 * OutDotNormal);
    // BRDF = D * F * Gs * Gm / (4 * Out.z * In.z)
    // Throughput = BRDF * In.z

    if (Parameters.RoughnessAlpha.x * Parameters.RoughnessAlpha.y < EPSILON)
    {
        InPDF = vec4(1.0);
    }
    else
    {
        float Gm = GGXSmithG1(Out, Parameters.RoughnessAlpha);
        float D = GGXDistribution(Normal, Parameters.RoughnessAlpha);
        InPDF = vec4(Gm * D / (4 * Out.z));
    }

    float Gs = GGXSmithG1(In, Parameters.RoughnessAlpha);

    Throughput = InPDF * Gs * F;

    return true;
}

vec4 BasicMetal_PDF(basic_metal_parameters Parameters, vec3 Out, vec3 In)
{
    if (Out.z <= 0.0 || In.z <= 0.0) return vec4(0.0);

    // Normal of the microfacet that would reflect Out to In.
    vec3 Half = SafeNormalize(Out + In);

    // NormalPDF = Gm * OutDotNormal * D / Out.z
    // InPDF = NormalPDF / (4 * OutDotNormal);

    if (Parameters.RoughnessAlpha.x * Parameters.RoughnessAlpha.y < EPSILON)
        return vec4(0.0);

    float Gm = GGXSmithG1(Out, Parameters.RoughnessAlpha);
    float D = GGXDistribution(Half, Parameters.RoughnessAlpha);
    return vec4(Gm * D / (4 * Out.z));
}
