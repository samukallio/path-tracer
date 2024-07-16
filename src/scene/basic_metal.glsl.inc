const uint BASIC_METAL_BASE_SPECTRUM                      = 1;
const uint BASIC_METAL_SPECULAR_SPECTRUM                  = 5;
const uint BASIC_METAL_ROUGHNESS                          = 9;
const uint BASIC_METAL_ROUGHNESS_ANISOTROPY               = 11;

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

bool BasicMetal_LoadMedium(uint MaterialIndex, vec4 Lambda, out medium Medium)
{
    return false;
}

bool BasicMetal_HasDiracBSDF(bsdf_parameters Parameters)
{
    return MaterialTexturableValue(Parameters, BASIC_METAL_ROUGHNESS) < 1e-3;
}

// Evaluate the BSDF for a given incident and scattered direction.
bool BasicMetal_EvaluateBSDF
(
    // Inputs.
    in  bsdf_parameters Parameters,
    in  vec3 In,
    in  vec3 Out,
    // Outputs.
    out vec4 Throughput,
    out vec4 Probability
)
{
    vec4 BaseReflectance;
    vec4 SpecularReflectance;
    vec2 RoughnessAlpha;
    bool SurfaceIsRough;
    BasicMetal_GetParameters(Parameters, BaseReflectance, SpecularReflectance, RoughnessAlpha, SurfaceIsRough);

    if (In.z <= 0.0 || Out.z <= 0.0 || !SurfaceIsRough) return false;

    vec3 Half = SafeNormalize(In + Out);

    // NormalPDF = Gm * OutDotNormal * D / Out.z
    // Probability = NormalPDF / (4 * OutDotNormal);
    //       = Gm * D / (4 * Out.z)

    // BRDF = D * F * Gs * Gm / (4 * Out.z * In.z)
    //      = Probability * F * Gs / In.z
    // Throughput = BRDF * In.z
    //            = Probability * F * Gs

    float Gm = GGXSmithG1(In, RoughnessAlpha);
    float D = GGXDistribution(Half, RoughnessAlpha);
    Probability = vec4(Gm * D / (4 * In.z));

    float Gs = GGXSmithG1(Out, RoughnessAlpha);
    vec4 F = SchlickFresnelMetal(BaseReflectance, SpecularReflectance, dot(In, Half));
    Throughput = Probability * Gs * F;

    return true;
}

// Importance sample a scattered direction and evaluate the BSDF using it.
bool BasicMetal_SampleBSDF
(
    // Inputs.
    in  bsdf_parameters Parameters,
    in  vec3 In,
    // Outputs.
    out vec3 Out,
    out vec4 Throughput,
    out vec4 Probability
)
{
    vec4 BaseReflectance;
    vec4 SpecularReflectance;
    vec2 RoughnessAlpha;
    bool SurfaceIsRough;
    BasicMetal_GetParameters(Parameters, BaseReflectance, SpecularReflectance, RoughnessAlpha, SurfaceIsRough);

    if (In.z <= 0.0) return false;

    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(In, RoughnessAlpha, NormalU1, NormalU2);
    float CosThetaIn = min(dot(Normal, In), 1.0);

    // Compute reflected direction.
    Out = 2 * CosThetaIn * Normal - In;

    // If the reflected direction is in the wrong hemisphere,
    // then it is shadowed and we terminate here.
    if (Out.z <= 0.0) return false;

    // NormalPDF = Gm * OutDotNormal * D / Out.z
    // Probability = NormalPDF / (4 * OutDotNormal);
    //       = Gm * D / (4 * Out.z)

    // BRDF = D * F * Gs * Gm / (4 * Out.z * In.z)
    //      = Probability * F * Gs / In.z
    // Throughput = BRDF * In.z
    //            = Probability * F * Gs

    Probability = vec4(1.0);

    if (SurfaceIsRough)
    {
        float Gm = GGXSmithG1(In, RoughnessAlpha);
        float D = GGXDistribution(Normal, RoughnessAlpha);
        Probability *= vec4(Gm * D / (4 * In.z));
    }

    float Gs = GGXSmithG1(Out, RoughnessAlpha);
    vec4 F = SchlickFresnelMetal(BaseReflectance, SpecularReflectance, CosThetaIn);
    Throughput = Probability * Gs * F;

    return true;
}
