const uint BASIC_TRANSLUCENT_IOR                   = 1;
const uint BASIC_TRANSLUCENT_ABBE_NUMBER           = 2;
const uint BASIC_TRANSLUCENT_ROUGHNESS             = 3;
const uint BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY  = 5;
const uint BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM = 7;
const uint BASIC_TRANSLUCENT_TRANSMISSION_DEPTH    = 10;
const uint BASIC_TRANSLUCENT_SCATTERING_SPECTRUM   = 11;
const uint BASIC_TRANSLUCENT_SCATTERING_ANISOTROPY = 14;

void BasicTranslucent_GetParameters
(
    in  bsdf_parameters Parameters,
    in  vec3 In,
    out vec4 RelativeIOR,
    out vec2 RoughnessAlpha,
    out bool SurfaceIsRough
)
{
    vec4 InteriorIOR = CauchyEmpiricalIOR
    (
        MaterialFloat(Parameters.MaterialIndex, BASIC_TRANSLUCENT_IOR),
        MaterialFloat(Parameters.MaterialIndex, BASIC_TRANSLUCENT_ABBE_NUMBER),
        Parameters.Lambda
    );

    if (In.z < 0.0)
        RelativeIOR = InteriorIOR / Parameters.ExteriorIOR;
    else
        RelativeIOR = Parameters.ExteriorIOR / InteriorIOR;

    RoughnessAlpha = GGXRoughnessAlpha
    (
        MaterialTexturableValue
        (
            Parameters.MaterialIndex,
            BASIC_TRANSLUCENT_ROUGHNESS,
            Parameters.TextureUV
        ),
        MaterialTexturableValue
        (
            Parameters.MaterialIndex,
            BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY,
            Parameters.TextureUV
        )
    );

    SurfaceIsRough = RoughnessAlpha.x * RoughnessAlpha.y > EPSILON;
}

vec3 BasicTranslucent_BaseColor(uint MaterialIndex, vec2 TextureUV)
{
    return MaterialColor(MaterialIndex, BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM);
}

bool BasicTranslucent_LoadMedium(uint MaterialIndex, vec4 Lambda, out medium Medium)
{
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

    return true;
}

bool BasicTranslucent_HasDiracBSDF(bsdf_parameters Parameters)
{
    return MaterialTexturableValue(Parameters.MaterialIndex, BASIC_TRANSLUCENT_ROUGHNESS, Parameters.TextureUV) < 1e-3;
}

// Evaluate the BSDF for a given incident and scattered direction.
bool BasicTranslucent_EvaluateBSDF
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
    vec4 RelativeIOR;
    vec2 RoughnessAlpha;
    bool SurfaceIsRough;
    BasicTranslucent_GetParameters(Parameters, In, RelativeIOR, RoughnessAlpha, SurfaceIsRough);

    if (!SurfaceIsRough)
    {
        Probability = vec4(0.0);
        Throughput = vec4(0.0);
        return true;
    }

    // Geometric masking term.
    float Gm = GGXSmithG1(In, RoughnessAlpha);

    if (In.z * Out.z > 0)
    {
        vec3 Half = SafeNormalize(Out + In);
        float CosThetaIn = dot(Half, In);

        vec4 F = FresnelDielectric(RelativeIOR, vec4(CosThetaIn));

        float D = GGXDistribution(Half, RoughnessAlpha);

        Probability = F * Gm * D / (4 * In.z);
    }
    else
    {
        // Compute the refraction half-vectors, i.e. the normals that
        // microfacets should have to produce the refraction in question
        // at each of the sampling wavelengths.  The vectors are computed
        // up to sign, which is inconsequential here due to symmetry.
        vec3 Half1 = SafeNormalize(Out + In * RelativeIOR.x);
        vec3 Half2 = SafeNormalize(Out + In * RelativeIOR.y);
        vec3 Half3 = SafeNormalize(Out + In * RelativeIOR.z);
        vec3 Half4 = SafeNormalize(Out + In * RelativeIOR.w);

        vec4 CosThetaIn  = vec4(dot(In , Half1), dot(In , Half2), dot(In , Half3), dot(In , Half4));
        vec4 CosThetaOut = vec4(dot(Out, Half1), dot(Out, Half2), dot(Out, Half3), dot(Out, Half4));

        // Surface reflectance at each sampling wavelength.
        vec4 F = FresnelDielectric(RelativeIOR, CosThetaIn, CosThetaOut);

        // GGX density for each half-vector.
        vec4 D = vec4(0.0);
        if (CosThetaIn.x * CosThetaOut.x < 0.0)
            D.x = GGXDistribution(Half1, RoughnessAlpha);
        if (CosThetaIn.y * CosThetaOut.y < 0.0)
            D.y = GGXDistribution(Half2, RoughnessAlpha);
        if (CosThetaIn.z * CosThetaOut.z < 0.0)
            D.z = GGXDistribution(Half3, RoughnessAlpha);
        if (CosThetaIn.w * CosThetaOut.w < 0.0)
            D.w = GGXDistribution(Half4, RoughnessAlpha);

        // Jacobian of the refraction operator for each sampling wavelength.
        vec4 J = abs(CosThetaOut) / Sqr(CosThetaIn * RelativeIOR + CosThetaOut);

        // Final PDFs.
        Probability = D * (1 - F) * Gm * J * abs(CosThetaIn / In.z);
    }

    // Geometric shadowing term.
    float Gs = GGXSmithG1(Out, RoughnessAlpha);

    // Radiance throughput.
    Throughput = Probability * Gs;

    return true;
}

// Importance sample a scattered direction and evaluate the BSDF using it.
bool BasicTranslucent_SampleBSDF
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
    vec4 RelativeIOR;
    vec2 RoughnessAlpha;
    bool SurfaceIsRough;
    BasicTranslucent_GetParameters(Parameters, In, RelativeIOR, RoughnessAlpha, SurfaceIsRough);

    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(In * sign(In.z), RoughnessAlpha, NormalU1, NormalU2);

    // Cosine of the angle of the incoming ray with respect to the microsurface normal.
    float CosThetaIn = clamp(dot(Normal, In), -1.0, +1.0);

    // Cosine of the angle of a refracted ray with respect to the microsurface normal,
    // using the relative IOR associated with the primary wavelength.
    float CosThetaRefracted = ComputeCosThetaRefracted(RelativeIOR.x, CosThetaIn);

    // Surface reflectance using the relative IOR associated with the primary wavelength.
    float Reflectance = FresnelDielectric(RelativeIOR.x, CosThetaIn, CosThetaRefracted);

    // Choose between reflection and refraction for this sample.
    if (Random0To1() < Reflectance)
    {
        // Reflected direction.
        Out = 2 * CosThetaIn * Normal - In;

        // If the microfacet reflects the ray through the macrosurface, then it is shadowed.
        if (Out.z * In.z <= 0) return false;

        // Surface reflectances at each sampling wavelength.
        vec4 F = FresnelDielectric(RelativeIOR, vec4(CosThetaIn));

        // NormalPDF = Gm * OutDotNormal * D / abs(Out.z)
        // Probability = NormalPDF / (4 * OutDotNormal);
        // BRDF = D * F * Gs * Gm / (4 * OutDotZ * abs(In.z))
        // Throughput = BRDF * abs(In.z)

        // The Fresnel coefficient varies by wavelength and is always incorporated into the PDF.
        Probability = F;

        // If the surface is rough, then we have a finite PDF for the reflected
        // direction, and should include it in the final PDF.  Otherwise, the
        // PDF of the reflected direction is a delta distribution, and we cannot
        // evaluate or represent it directly.  In that case, the interpretation
        // of the resulting PDF and throughput is that they are coefficients of
        // hidden, implied delta distributions.  This must be taken into account
        // then combining results from multiple BSDFs.
        if (SurfaceIsRough)
        {
            float Gm = GGXSmithG1(In, RoughnessAlpha);
            float D = GGXDistribution(Normal, RoughnessAlpha);
            Probability *= Gm * D / (4 * abs(In.z));
        }

        // Geometric shadowing term.
        float Gs = GGXSmithG1(Out, RoughnessAlpha);

        // Radiance throughput.
        Throughput = Probability * Gs;

        return true;
    }

    // Compute refracted direction.
    Out = (CosThetaRefracted + RelativeIOR.x * CosThetaIn) * Normal - RelativeIOR.x * In;

    // If the microfacet refracts the ray back out of the macrosurface, then it is shadowed.
    if (Out.z * In.z >= 0) return false;

    // If the surface is rough, then it is possible for this same refraction
    // to have been generated at a secondary wavelength but with a different
    // microsurface normal.  In that case we compute the alternate probabilities
    // and continue with on with the full set of sampling wavelengths.
    if (SurfaceIsRough)
    {
        // Compute the microfacet surface normals that would be necessary
        // to cause the same refraction to occur but for the secondary
        // wavelengths.
        vec3 Normal2 = SafeNormalize(Out + In * RelativeIOR.y);
        vec3 Normal3 = SafeNormalize(Out + In * RelativeIOR.z);
        vec3 Normal4 = SafeNormalize(Out + In * RelativeIOR.w);

        // Cosines of angles of the incoming direction with respect to each microsurface normal.
        vec4 CosThetaIn4 = vec4(
            CosThetaIn,
            dot(In, Normal2),
            dot(In, Normal3),
            dot(In, Normal4));

        // Cosines of angles of the refracted direction with respect to each microsurface normal.
        vec4 CosThetaOut4 = vec4(
            CosThetaRefracted,
            dot(Out, Normal2),
            dot(Out, Normal3),
            dot(Out, Normal4));

        // Surface reflectances at each sampling wavelength.
        vec4 F = FresnelDielectric(RelativeIOR, CosThetaIn4, CosThetaOut4);

        // GGX densities of the microfact normals.
        vec4 D = vec4(0.0);
        D.x = GGXDistribution(Normal, RoughnessAlpha);

        // Densities for the secondary wavelengths. We also need to check
        // that the generated normals are actually plausible (no total
        // internal reflection).  Otherwise, the refraction is impossible
        // and the corresponding density is zero.
        if (CosThetaIn4.y * CosThetaOut4.y < 0.0)
            D.y = GGXDistribution(Normal2, RoughnessAlpha);
        if (CosThetaIn4.z * CosThetaOut4.z < 0.0)
            D.z = GGXDistribution(Normal3, RoughnessAlpha);
        if (CosThetaIn4.w * CosThetaOut4.w < 0.0)
            D.w = GGXDistribution(Normal4, RoughnessAlpha);

        // Geometric masking term.
        float Gm = GGXSmithG1(In, RoughnessAlpha);

        //
        // NormalPDF = Gm * abs(CosThetaIn) * D / abs(In.z)
        //

        // Jacobian of the refraction operator:
        //
        // J = abs(CosThetaOut) / (CosThetaIn * RelativeIOR + CosThetaOut)^2
        //

        //
        // Probability = (1 - F) * J * NormalPDF
        //       = (1 - F) * J * Gm * abs(CosThetaIn) * D / abs(In.z)
        //

        //
        // BTDF = D * (1 - F) * Gs * Gm * J * abs(CosThetaIn) / abs(In.z * Out.z)
        //      = Probability * Gs  / abs(Out.z)
        //
        //
        // Throughput = BTDF * abs(Out.z)
        //            = Probability * Gs
        //

        vec4 J = abs(CosThetaOut4) / Sqr(CosThetaIn4 * RelativeIOR + CosThetaOut4);
        Probability = D * (1 - F) * Gm * J * abs(CosThetaIn4 / In.z);
    }
    else
    {
        // The surface is perfectly smooth, so the probability of generating
        // this refraction with any of the secondary wavelengths is zero.
        // We continue with just the primary wavelength.
        Probability = vec4(1 - Reflectance, 0, 0, 0);
    }

    float Gs = GGXSmithG1(Out, RoughnessAlpha);

    Throughput = Probability * Gs;

    return true;
}
