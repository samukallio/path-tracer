const uint BASIC_METAL_BASE_SPECTRUM                      = 1;
const uint BASIC_METAL_SPECULAR_SPECTRUM                  = 5;
const uint BASIC_METAL_ROUGHNESS                          = 9;
const uint BASIC_METAL_ROUGHNESS_TEXTURE_INDEX            = 10;
const uint BASIC_METAL_ROUGHNESS_ANISOTROPY               = 11;
const uint BASIC_METAL_ROUGHNESS_ANISOTROPY_TEXTURE_INDEX = 12;

void BasicMetal_GetParameters
(
    in  bsdf_parameters Parameters,
    out vec4 BaseReflectance,
    out vec4 SpecularReflectance,
    out vec2 RoughnessAlpha,
    out bool SurfaceIsRough
)
{
    BaseReflectance = MaterialTexturableReflectance(Parameters, BASIC_METAL_BASE_SPECTRUM);

    SpecularReflectance = MaterialTexturableReflectance(Parameters, BASIC_METAL_SPECULAR_SPECTRUM);

    RoughnessAlpha = GGXRoughnessAlpha
    (
        MaterialTexturableValue(Parameters, BASIC_METAL_ROUGHNESS),
        MaterialTexturableValue(Parameters, BASIC_METAL_ROUGHNESS_ANISOTROPY)
    );

    SurfaceIsRough = RoughnessAlpha.x * RoughnessAlpha.y > EPSILON;
}

vec3 BasicMetal_BaseColor(uint MaterialIndex, vec2 TextureUV)
{
    return MaterialTexturableColor(MaterialIndex, BASIC_METAL_BASE_SPECTRUM, TextureUV);
}

bool BasicMetal_HasDiracBSDF(bsdf_parameters Parameters)
{
    return MaterialTexturableValue(Parameters, BASIC_METAL_ROUGHNESS) < 1e-3;
}

bool BasicMetal_EvaluateBSDF
(
    in  bsdf_parameters Parameters,
    in  vec3 Out,
    in  vec3 In,
    out vec4 Throughput,
    out vec4 InPDF
)
{
    vec4 BaseReflectance;
    vec4 SpecularReflectance;
    vec2 RoughnessAlpha;
    bool SurfaceIsRough;
    BasicMetal_GetParameters(Parameters, BaseReflectance, SpecularReflectance, RoughnessAlpha, SurfaceIsRough);

    if (Out.z <= 0.0 || In.z <= 0.0 || !SurfaceIsRough) return false;

    vec3 Half = SafeNormalize(Out + In);

    // NormalPDF = Gm * OutDotNormal * D / Out.z
    // InPDF = NormalPDF / (4 * OutDotNormal);
    //       = Gm * D / (4 * Out.z)

    // BRDF = D * F * Gs * Gm / (4 * Out.z * In.z)
    //      = InPDF * F * Gs / In.z
    // Throughput = BRDF * In.z
    //            = InPDF * F * Gs

    float Gm = GGXSmithG1(Out, RoughnessAlpha);
    float D = GGXDistribution(Half, RoughnessAlpha);
    InPDF = vec4(Gm * D / (4 * Out.z));

    float Gs = GGXSmithG1(In, RoughnessAlpha);
    vec4 F = SchlickFresnelMetal(BaseReflectance, SpecularReflectance, dot(Out, Half));
    Throughput = InPDF * Gs * F;

    return true;
}

bool BasicMetal_SampleBSDF
(
    in  bsdf_parameters Parameters,
    in  vec3 Out,
    out vec3 In,
    out vec4 Throughput,
    out vec4 InPDF
)
{
    vec4 BaseReflectance;
    vec4 SpecularReflectance;
    vec2 RoughnessAlpha;
    bool SurfaceIsRough;
    BasicMetal_GetParameters(Parameters, BaseReflectance, SpecularReflectance, RoughnessAlpha, SurfaceIsRough);

    if (Out.z <= 0.0) return false;

    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(Out, RoughnessAlpha, NormalU1, NormalU2);
    float OutDotNormal = min(dot(Normal, Out), 1.0);

    // Compute reflected direction.
    In = 2 * OutDotNormal * Normal - Out;

    // If the reflected direction is in the wrong hemisphere,
    // then it is shadowed and we terminate here.
    if (In.z <= 0.0) return false;

    // NormalPDF = Gm * OutDotNormal * D / Out.z
    // InPDF = NormalPDF / (4 * OutDotNormal);
    //       = Gm * D / (4 * Out.z)

    // BRDF = D * F * Gs * Gm / (4 * Out.z * In.z)
    //      = InPDF * F * Gs / In.z
    // Throughput = BRDF * In.z
    //            = InPDF * F * Gs

    InPDF = vec4(1.0);

    if (SurfaceIsRough)
    {
        float Gm = GGXSmithG1(Out, RoughnessAlpha);
        float D = GGXDistribution(Normal, RoughnessAlpha);
        InPDF *= vec4(Gm * D / (4 * Out.z));
    }

    float Gs = GGXSmithG1(In, RoughnessAlpha);
    vec4 F = SchlickFresnelMetal(BaseReflectance, SpecularReflectance, OutDotNormal);
    Throughput = InPDF * Gs * F;

    return true;
}
