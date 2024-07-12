const uint BASIC_TRANSLUCENT_IOR                   = 1;
const uint BASIC_TRANSLUCENT_ABBE_NUMBER           = 2;
const uint BASIC_TRANSLUCENT_ROUGHNESS             = 3;
const uint BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY  = 5;
const uint BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM = 7;
const uint BASIC_TRANSLUCENT_TRANSMISSION_DEPTH    = 10;
const uint BASIC_TRANSLUCENT_SCATTERING_SPECTRUM   = 11;
const uint BASIC_TRANSLUCENT_SCATTERING_ANISOTROPY = 14;

struct basic_translucent_parameters
{
    vec4  Lambda;                 // Sampling wavelengths.
    vec4  RelativeIOR;            //
    vec2  RoughnessAlpha;         // GGX roughness alpha (X and Y).
};

vec3 BasicTranslucent_BaseColor(uint MaterialIndex, vec2 TextureUV)
{
    return MaterialColor(MaterialIndex, BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM);
}

medium BasicTranslucent_Medium(uint MaterialIndex, vec4 Lambda)
{
    medium Medium;

    Medium.IOR = CauchyEmpiricalIOR
    (
        MaterialFloat(MaterialIndex, BASIC_TRANSLUCENT_IOR),
        MaterialFloat(MaterialIndex, BASIC_TRANSLUCENT_ABBE_NUMBER),
        Lambda
    );

    float TransmissionDepth = MaterialFloat(MaterialIndex, BASIC_TRANSLUCENT_TRANSMISSION_DEPTH);

    if (TransmissionDepth > 0.0)
    {
        vec4 ExtinctionRate = -log(SampleParametricSpectrum(MaterialVec3(MaterialIndex, BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM), Lambda)) / TransmissionDepth;
        vec4 ScatteringRate = SampleParametricSpectrum(MaterialVec3(MaterialIndex, BASIC_TRANSLUCENT_SCATTERING_SPECTRUM), Lambda) / TransmissionDepth;
        Medium.AbsorptionRate = max(ExtinctionRate - ScatteringRate, 0.0);
        Medium.ScatteringRate = ScatteringRate;
        Medium.ScatteringAnisotropy = MaterialFloat(MaterialIndex, BASIC_TRANSLUCENT_SCATTERING_ANISOTROPY);
    }
    else
    {
        Medium.AbsorptionRate = vec4(0.0);
        Medium.ScatteringRate = vec4(0.0);
        Medium.ScatteringAnisotropy = 0.0;
    }

    return Medium;
}

basic_translucent_parameters BasicTranslucent_Parameters
(
    uint MaterialIndex,
    vec2 TextureUV,
    vec4 Lambda,
    vec4 ExteriorIOR
)
{
    basic_translucent_parameters Parameters;

    Parameters.Lambda = Lambda;

    vec4 InteriorIOR = CauchyEmpiricalIOR
    (
        MaterialFloat(MaterialIndex, BASIC_TRANSLUCENT_IOR),
        MaterialFloat(MaterialIndex, BASIC_TRANSLUCENT_ABBE_NUMBER),
        Lambda
    );

    Parameters.RelativeIOR = ExteriorIOR / InteriorIOR;

    Parameters.RoughnessAlpha = GGXRoughnessAlpha
    (
        MaterialTexturableValue
        (
            MaterialIndex,
            BASIC_TRANSLUCENT_ROUGHNESS,
            TextureUV
        ),
        MaterialTexturableValue
        (
            MaterialIndex,
            BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY,
            TextureUV
        )
    );

    return Parameters;
}

void BasicTranslucent_Sample
(
    basic_translucent_parameters Parameters,
    vec3 Out, out vec3 In,
    inout vec4 PathThroughput,
    inout vec4 PathDensity
)
{
    vec4 RelativeIOR = Parameters.RelativeIOR;
    if (Out.z < 0) RelativeIOR = 1.0 / RelativeIOR;

    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(Out * sign(Out.z), Parameters.RoughnessAlpha, NormalU1, NormalU2);
    float Cosine = clamp(dot(Normal, Out), -1.0, +1.0);

    // Compute the cosine of the angle between the refraction direction and
    // the microsurface normal. The squared cosine is clamped to zero, the
    // boundary for total internal reflection (TIR). When the cosine is zero,
    // the Fresnel formulas give a reflectivity of 1, producing a TIR without
    // the need for branches.
    float RefractedCosineSquared = 1 - RelativeIOR.x * RelativeIOR.x * (1 - Cosine * Cosine);
    float RefractedCosine = -sign(Out.z) * sqrt(max(RefractedCosineSquared, 0.0));

    // Compute dielectric reflectance.
    float Reflectance = FresnelDielectric(RefractedCosine, Cosine, RelativeIOR.x);

    // Specular reflection?
    if (Random0To1() < Reflectance)
    {
        // Compute reflected direction.
        In = 2 * Cosine * Normal - Out;

        // If the reflected direction is in the wrong hemisphere,
        // then it is shadowed and we terminate here.
        if (In.z * Out.z <= 0)
        {
            PathDensity = vec4(0.0);
            return;
        }

        PathThroughput *= GGXSmithG1(In, Parameters.RoughnessAlpha);
        return;
    }

    // Compute refracted direction.
    In = (RelativeIOR.x * Cosine + RefractedCosine) * Normal - RelativeIOR.x * Out;

    // If the refracted direction is in the wrong hemisphere,
    // then it is shadowed and we terminate here.
    if (In.z * Out.z > 0)
    {
        PathDensity = vec4(0.0);
        return;
    }

    float Shadowing = GGXSmithG1(In, Parameters.RoughnessAlpha);

    // If the surface is rough, then a refraction with the same incoming
    // and outgoing direction is possible for the secondary wavelengths,
    // although with different probabilities.
    if (length(Parameters.RoughnessAlpha) > EPSILON)
    {
        // Compute the Fresnel terms for all wavelengths.
        vec4 Fresnel = FresnelDielectric(RefractedCosine, Cosine, RelativeIOR);

        // Compute the microfacet surface normals that would be necessary
        // to cause the same refraction to occur but for the secondary
        // wavelengths. Note that the orientation of the normals might
        // be wrong, but it doesn't matter here, since GGXDistribution()
        // has reflection symmetry.
        vec3 Normal2 = SafeNormalize(In + Out * RelativeIOR.y);
        vec3 Normal3 = SafeNormalize(In + Out * RelativeIOR.z);
        vec3 Normal4 = SafeNormalize(In + Out * RelativeIOR.w);

        // Now figure out the GGX densities of the normals.
        vec4 Density = vec4(0.0);

        // Density of the normal corresponding to the primary wavelength.
        Density.x = GGXDistribution(Normal, Parameters.RoughnessAlpha);

        // Densities for the secondary wavelengths. We also need to check
        // that the generated normals are actually plausible (no total
        // internal reflection). Otherwise, the refraction is impossible
        // and the corresponding density is zero.
        if (dot(In, Normal2) * dot(Out, Normal2) < 0.0)
            Density.y = GGXDistribution(Normal2, Parameters.RoughnessAlpha);
        if (dot(In, Normal3) * dot(Out, Normal3) < 0.0)
            Density.z = GGXDistribution(Normal3, Parameters.RoughnessAlpha);
        if (dot(In, Normal4) * dot(Out, Normal4) < 0.0)
            Density.w = GGXDistribution(Normal4, Parameters.RoughnessAlpha);

        // Scale the densities towards unity to mitigate numerical problems.
        Density /= max(EPSILON, max4(Density));

        PathThroughput *= Density * Fresnel * Shadowing;
        PathDensity *= Density * Fresnel;
    }
    else
    {
        // The surface is perfectly smooth, so the probability of generating
        // this refraction with any of the secondary wavelengths is zero.
        // We continue with just the primary wavelength.
        PathThroughput.x *= Shadowing;
        PathThroughput.yzw = vec3(0.0);
        PathDensity.yzw = vec3(0.0);
    }
}
