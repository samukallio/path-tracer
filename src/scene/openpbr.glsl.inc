const uint OPENPBR_OPACITY = 0;
const uint OPENPBR_LAYER_BOUNCE_LIMIT = 1;
const uint OPENPBR_BASE_WEIGHT = 2;
const uint OPENPBR_BASE_SPECTRUM = 3;
const uint OPENPBR_BASE_SPECTRUM_TEXTURE_INDEX = 6;
const uint OPENPBR_BASE_METALNESS = 7;
const uint OPENPBR_BASE_DIFFUSE_ROUGHNESS = 8;
const uint OPENPBR_SPECULAR_WEIGHT = 9;
const uint OPENPBR_SPECULAR_SPECTRUM = 10;
const uint OPENPBR_SPECULAR_IOR = 13;
const uint OPENPBR_SPECULAR_ROUGHNESS = 14;
const uint OPENPBR_SPECULAR_ROUGHNESS_TEXTURE_INDEX = 15;
const uint OPENPBR_SPECULAR_ROUGHNESS_ANISOTROPY = 16;
const uint OPENPBR_TRANSMISSION_SPECTRUM = 17;
const uint OPENPBR_TRANSMISSION_WEIGHT = 20;
const uint OPENPBR_TRANSMISSION_SCATTER_SPECTRUM = 21;
const uint OPENPBR_TRANSMISSION_SCATTER_ANISOTROPY = 24;
const uint OPENPBR_TRANSMISSION_DEPTH = 25;
const uint OPENPBR_TRANSMISSION_DISPERSION_ABBE_NUMBER = 26;
const uint OPENPBR_EMISSION_SPECTRUM = 27;
const uint OPENPBR_EMISSION_SPECTRUM_TEXTURE_INDEX = 30;
const uint OPENPBR_EMISSION_LUMINANCE = 31;
const uint OPENPBR_COAT_WEIGHT = 32;
const uint OPENPBR_COAT_COLOR_SPECTRUM = 33;
const uint OPENPBR_COAT_IOR = 36;
const uint OPENPBR_COAT_ROUGHNESS = 37;
const uint OPENPBR_COAT_ROUGHNESS_ANISOTROPY = 38;
const uint OPENPBR_COAT_DARKENING = 39;

// Local surface material properties for the OpenPBR BSDF.
struct openpbr_surface
{
    float Opacity; // Opacity of the surface geometry.
    uint LayerBounceLimit; // Maximum number of BSDF layer bounces.
    bool BaseIsMetal; // Is the base substrate layer in the BSDF stack metal?
    bool BaseIsTranslucent; // Is the base substrate layer in the BSDF stack a translucent dielectric?
    vec4 BaseReflectance; // Base reflectance value at each of the 4 sampling wavelengths.
    float BaseDiffuseRoughness; // Oren-Nayar roughness for the glossy-diffuse base substrate layer.
    bool CoatIsPresent; // Is there a dielectric coat layer over the base substrate?
    vec4 CoatRelativeIOR; // Relative IOR of the coat layer at each of the 4 sampling wavelengths.
    vec4 CoatTransmittance; // Transmittance of the coat layer at each of the 4 sampling wavelengths.
    vec2 CoatRoughnessAlpha; // Coat GGX roughness alpha (X and Y).
    float SpecularWeight; // Artistic specular weight parameter.
    vec4 SpecularRelativeIOR; // Relative IOR of a dielectric base substrate at each of the 4 sampling wavelengths.
    vec4 SpecularReflectance; // 82-degree reflectance of the metal base substrate at each of the 4 sampling wavelengths.
    vec2 SpecularRoughnessAlpha; // Base substrate GGX roughness alpha (X and Y).
    vec4 Emission; // Base substrate emission radiance at each of the 4 sampling wavelengths.
};

vec3 OpenPBRBaseColor(hit Hit)
{
    uint MI = Hit.MaterialIndex;

    vec3 BaseColor = ObserveParametricSpectrumUnderD65(MaterialVec3(MI, OPENPBR_BASE_SPECTRUM));
    uint BaseSpectrumTextureIndex = MaterialUint(MI, OPENPBR_BASE_SPECTRUM_TEXTURE_INDEX);

    if (BaseSpectrumTextureIndex != TEXTURE_INDEX_NONE) {
        vec4 Value = SampleTexture(BaseSpectrumTextureIndex, Hit.UV);
        BaseColor *= ObserveParametricSpectrumUnderD65(Value.xyz);
    }

    return BaseColor;
}

// Evaluate the surface properties of a hit surface at given wavelengths.
openpbr_surface OpenPBRResolveSurface(hit Hit, vec4 Lambda, vec4 ExteriorIOR)
{
    openpbr_surface Surface;

    uint MI = Hit.MaterialIndex;

    // Geometric opacity.
    Surface.Opacity = MaterialFloat(MI, OPENPBR_OPACITY);

    // Surface composition.
    Surface.CoatIsPresent = Random0To1() < MaterialFloat(MI, OPENPBR_COAT_WEIGHT);
    Surface.BaseIsMetal = Random0To1() < MaterialFloat(MI, OPENPBR_BASE_METALNESS);
    Surface.BaseIsTranslucent = !Surface.BaseIsMetal && Random0To1() < MaterialFloat(MI, OPENPBR_TRANSMISSION_WEIGHT);

    // Base reflectance and opacity.
    Surface.BaseReflectance = MaterialFloat(MI, OPENPBR_BASE_WEIGHT) * SampleParametricSpectrum(MaterialVec3(MI, OPENPBR_BASE_SPECTRUM), Lambda);
    Surface.BaseDiffuseRoughness = MaterialFloat(MI, OPENPBR_BASE_DIFFUSE_ROUGHNESS);

    uint BaseSpectrumTextureIndex = MaterialUint(MI, OPENPBR_BASE_SPECTRUM_TEXTURE_INDEX);
    if (BaseSpectrumTextureIndex != TEXTURE_INDEX_NONE)
    {
        vec4 Value = SampleTexture(BaseSpectrumTextureIndex, Hit.UV);
        Surface.BaseReflectance *= SampleParametricSpectrum(Value.xyz, Lambda);
        Surface.Opacity *= Value.a;
    }

    // Coat.
    if (Surface.CoatIsPresent)
    {
        Surface.CoatRelativeIOR = ExteriorIOR / MaterialFloat(MI, OPENPBR_COAT_IOR);
        Surface.CoatTransmittance = SampleParametricSpectrum
        (
            MaterialVec3(MI, OPENPBR_COAT_COLOR_SPECTRUM),
            Lambda
        );
        Surface.CoatRoughnessAlpha = GGXRoughnessAlpha
        (
            MaterialFloat(MI, OPENPBR_COAT_ROUGHNESS),
            MaterialFloat(MI, OPENPBR_COAT_ROUGHNESS_ANISOTROPY)
        );
    }

    // Specular.
    Surface.SpecularWeight = MaterialFloat(MI, OPENPBR_SPECULAR_WEIGHT);

    Surface.SpecularReflectance = SampleParametricSpectrum
    (
        MaterialVec3(MI, OPENPBR_SPECULAR_SPECTRUM),
        Lambda
    );

    vec4 SpecularIOR = CauchyEmpiricalIOR
    (
        MaterialFloat(MI, OPENPBR_SPECULAR_IOR),
        MaterialFloat(MI, OPENPBR_TRANSMISSION_DISPERSION_ABBE_NUMBER),
        Lambda
    );

    if (Surface.CoatIsPresent)
        Surface.SpecularRelativeIOR = MaterialFloat(MI, OPENPBR_COAT_IOR) / SpecularIOR;
    else
        Surface.SpecularRelativeIOR = ExteriorIOR / SpecularIOR;

    float SpecularRoughness = MaterialFloat(MI, OPENPBR_SPECULAR_ROUGHNESS);
    uint SpecularRoughnessTextureIndex = MaterialUint(MI, OPENPBR_SPECULAR_ROUGHNESS_TEXTURE_INDEX);

    if (SpecularRoughnessTextureIndex != TEXTURE_INDEX_NONE)
    {
        vec4 Value = SampleTexture(SpecularRoughnessTextureIndex, Hit.UV);
        SpecularRoughness *= Value.r;
    }

    Surface.SpecularRoughnessAlpha = GGXRoughnessAlpha
    (
        SpecularRoughness,
        MaterialFloat(MI, OPENPBR_SPECULAR_ROUGHNESS_ANISOTROPY)
    );

    //
    Surface.Emission
        = SampleParametricSpectrum(MaterialVec3(MI, OPENPBR_EMISSION_SPECTRUM), Lambda)
        * MaterialFloat(MI, OPENPBR_EMISSION_LUMINANCE);

    uint EmissionSpectrumTextureIndex = MaterialUint(MI, OPENPBR_EMISSION_SPECTRUM_TEXTURE_INDEX);
    if (EmissionSpectrumTextureIndex != TEXTURE_INDEX_NONE)
    {
        vec4 Value = SampleTexture(EmissionSpectrumTextureIndex, Hit.UV);
        Surface.Emission *= SampleParametricSpectrum(Value, Lambda);
    }

    //
    Surface.LayerBounceLimit = MaterialUint(MI, OPENPBR_LAYER_BOUNCE_LIMIT);

    return Surface;
}

void OpenPBRResolveMedium(uint MaterialIndex, vec4 Lambda, out medium Medium)
{
    uint MI = MaterialIndex;

    Medium.IOR = CauchyEmpiricalIOR
    (
        MaterialFloat(MI, OPENPBR_SPECULAR_IOR),
        MaterialFloat(MI, OPENPBR_TRANSMISSION_DISPERSION_ABBE_NUMBER),
        Lambda
    );

    float TransmissionDepth = MaterialFloat(MI, OPENPBR_TRANSMISSION_DEPTH);

    if (TransmissionDepth > 0.0)
    {
        vec4 ExtinctionRate = -log(SampleParametricSpectrum(MaterialVec3(MI, OPENPBR_TRANSMISSION_SPECTRUM), Lambda)) / TransmissionDepth;
        vec4 ScatteringRate = SampleParametricSpectrum(MaterialVec3(MI, OPENPBR_TRANSMISSION_SCATTER_SPECTRUM), Lambda) / TransmissionDepth;
        Medium.AbsorptionRate = max(ExtinctionRate - ScatteringRate, 0.0);
        Medium.ScatteringRate = ScatteringRate;
        Medium.ScatteringAnisotropy = MaterialFloat(MI, OPENPBR_TRANSMISSION_SCATTER_ANISOTROPY);
    }
    else
    {
        Medium.AbsorptionRate = vec4(0.0);
        Medium.ScatteringRate = vec4(0.0);
        Medium.ScatteringAnisotropy = 0.0;
    }
}

// OpenPBR coat BSDF.
void OpenPBRCoatBSDF(openpbr_surface Surface, vec4 Lambda, vec3 Out, out vec3 In, inout path Path)
{
    if (!Surface.CoatIsPresent)
    {
        In = -Out;
        return;
    }

    // Sample a microsurface normal for coat scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(Out * sign(Out.z), Surface.CoatRoughnessAlpha, NormalU1, NormalU2);
    float Cosine = dot(Normal, Out);

    // Substrate is a dielectric.
    vec4 RelativeIOR = Surface.CoatRelativeIOR;
    if (Out.z < 0) RelativeIOR = 1.0 / RelativeIOR;

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
            Path.Weight = vec4(0.0);
            return;
        }

        Path.Throughput *= GGXSmithG1(In, Surface.CoatRoughnessAlpha);

        // If the outgoing ray points down, then the ray is being reflected
        // within the coat medium. Apply the coat absorption factor for both
        // the incoming and outgoing direction. The path length of a ray
        // within the coat depends on the angle of the ray with respect to
        // the surface normal. Here, the angle is measured with respect to
        // the macrosurface normal, as the thickness of the coat layer is
        // oriented along the macrosurface.
        if (Out.z < 0)
        {
            float Exponent = -(0.5 / Out.z + 0.5 / In.z);
            Path.Throughput *= pow(Surface.CoatTransmittance, vec4(Exponent));
        }
    }
    else
    {
        // Compute refracted direction.
        In = (RelativeIOR.x * Cosine + RefractedCosine) * Normal - RelativeIOR.x * Out;

        // If the refracted direction is in the wrong hemisphere,
        // then it is shadowed and we terminate here.
        if (In.z * Out.z > 0)
        {
            Path.Weight = vec4(0.0);
            return;
        }

        Path.Throughput *= GGXSmithG1(In, Surface.CoatRoughnessAlpha);

        // The ray is traversing the coat medium. Apply the coat absorption
        // factor based on the distance traversed in the coat medium. If the
        // outgoing ray points down, then it is the outgoing ray that passes
        // through the medium, and we determine the path length using that.
        // Otherwise, it is the incoming ray that traverses the medium.
        if (Out.z < 0)
            Path.Throughput *= pow(Surface.CoatTransmittance, vec4(-0.5 / Out.z));
        else
            Path.Throughput *= pow(Surface.CoatTransmittance, vec4(-0.5 / In.z));
    }
}

// Specular part of the OpenPBR base substrate BSDF.
void OpenPBRBaseSpecularBSDF(openpbr_surface Surface, vec4 Lambda, vec3 Out, out vec3 In, inout path Path)
{
    if (Out.z > 0)
    {
        float ClusterPDF = Path.Weight.x + Path.Weight.y + Path.Weight.z + Path.Weight.w;
        Path.Sample += SampleStandardObserver(Lambda) * (Surface.Emission * Path.Throughput) / ClusterPDF;
    }

    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = GGXVisibleNormal(Out * sign(Out.z), Surface.SpecularRoughnessAlpha, NormalU1, NormalU2);
    float Cosine = dot(Normal, Out);

    // Metal base substrate.
    if (Surface.BaseIsMetal)
    {
        // Compute reflected direction.
        In = 2 * Cosine * Normal - Out;

        // If the reflected direction is in the wrong hemisphere,
        // then it is shadowed and we terminate here.
        if (Out.z * In.z <= 0)
        {
            Path.Weight = vec4(0.0);
            return;
        }

        float Shadowing = GGXSmithG1(Out, Surface.SpecularRoughnessAlpha);

        vec4 Fresnel = Surface.SpecularWeight * SchlickFresnelMetal
        (
            Surface.BaseReflectance,
            Surface.SpecularReflectance,
            abs(Cosine)
        );

        Path.Throughput *= Fresnel * Shadowing;
    }
    // Dielectric base substrate.
    else {
        vec4 RelativeIOR = Surface.SpecularRelativeIOR;
        if (Out.z < 0) RelativeIOR = 1.0 / RelativeIOR;

        // Modulation of the relative IOR by the specular weight parameter.
        if (Surface.SpecularWeight < 1.0)
        {
            vec4 R = sqrt(Surface.SpecularWeight) * (1.0 - RelativeIOR) / (1.0 + RelativeIOR);
            RelativeIOR = (1.0 - R) / (1.0 + R);
        }

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
                Path.Weight = vec4(0.0);
                return;
            }

            // Per the OpenPBR specification: the specular color material
            // parameter modulates the Fresnel factor of the dielectric,
            // but only for reflections from above (and not below).
            if (Out.z > 0) Path.Throughput *= Surface.SpecularReflectance;

            Path.Throughput *= GGXSmithG1(In, Surface.SpecularRoughnessAlpha);
        }
        else
        {
            // Compute refracted direction.
            In = (RelativeIOR.x * Cosine + RefractedCosine) * Normal - RelativeIOR.x * Out;

            // If the refracted direction is in the wrong hemisphere,
            // then it is shadowed and we terminate here.
            if (In.z * Out.z > 0)
            {
                Path.Weight = vec4(0.0);
                return;
            }

            float Shadowing = GGXSmithG1(In, Surface.SpecularRoughnessAlpha);

            // If the surface is rough, then a refraction with the same incoming
            // and outgoing direction is possible for the secondary wavelengths,
            // although with different probabilities.
            if (length(Surface.SpecularRoughnessAlpha) > EPSILON)
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
                Density.x = GGXDistribution(Normal, Surface.SpecularRoughnessAlpha);

                // Densities for the secondary wavelengths. We also need to check
                // that the generated normals are actually plausible (no total
                // internal reflection). Otherwise, the refraction is impossible
                // and the corresponding density is zero.
                if (dot(In, Normal2) * dot(Out, Normal2) < 0.0)
                    Density.y = GGXDistribution(Normal2, Surface.SpecularRoughnessAlpha);
                if (dot(In, Normal3) * dot(Out, Normal3) < 0.0)
                    Density.z = GGXDistribution(Normal3, Surface.SpecularRoughnessAlpha);
                if (dot(In, Normal4) * dot(Out, Normal4) < 0.0)
                    Density.w = GGXDistribution(Normal4, Surface.SpecularRoughnessAlpha);

                // Scale the densities towards unity to mitigate numerical problems.
                Density /= max(EPSILON, max4(Density));

                Path.Throughput *= Density * Fresnel * Shadowing;
                Path.Weight *= Density * Fresnel;
            }
            else
            {
                // The surface is perfectly smooth, so the probability of generating
                // this refraction with any of the secondary wavelengths is zero.
                // We continue with just the primary wavelength.
                Path.Throughput.x *= Shadowing;
                Path.Throughput.yzw = vec3(0.0);
                Path.Weight.yzw = vec3(0.0);
            }
        }
    }
}

// Diffuse part of the OpenPBR base substrate BSDF.
void OpenPBRBaseDiffuseBSDF(openpbr_surface Surface, vec4 Lambda, vec3 Out, out vec3 In, inout path Path)
{
    if (Surface.BaseIsTranslucent)
    {
        In = -Out;
        return;
    }

    In = SafeNormalize(RandomDirection() + vec3(0, 0, 1));

    float S = dot(In, Out) - In.z * Out.z;
    float T = S > 0 ? max(In.z, Out.z) : 1.0;
    float SigmaSq = Surface.BaseDiffuseRoughness * Surface.BaseDiffuseRoughness;
    vec4 A = 1 - 0.5 * SigmaSq / (SigmaSq + 0.33) + 0.17 * Surface.BaseReflectance * SigmaSq / (SigmaSq + 0.13);
    float B = 0.45 * SigmaSq / (SigmaSq + 0.09);

    Path.Throughput *= Surface.BaseReflectance * (A + B * S / T);
}

void OpenPBRBSDF(openpbr_surface Surface, vec4 Lambda, vec3 Out, out vec3 In, inout path Path)
{
    const int LAYER_EXTERNAL = -1;
    const int LAYER_COAT = 0;
    const int LAYER_BASE_SPECULAR = 1;
    const int LAYER_BASE_DIFFUSE = 2;

    int Layer;
    
    if (Out.z > 0)
        Layer = Surface.CoatIsPresent ? LAYER_COAT : LAYER_BASE_SPECULAR;
    else
        Layer = LAYER_BASE_SPECULAR;

    for (int I = 0; I < Surface.LayerBounceLimit; I++)
    {
        if (Layer == LAYER_COAT)
        {
            OpenPBRCoatBSDF(Surface, Lambda, Out, In, Path);
            Layer = In.z < 0 ? LAYER_BASE_SPECULAR : LAYER_EXTERNAL;
        }
        else if (Layer == LAYER_BASE_SPECULAR)
        {
            OpenPBRBaseSpecularBSDF(Surface, Lambda, Out, In, Path);
            Layer = In.z < 0 ? LAYER_BASE_DIFFUSE : LAYER_COAT;
        }
        else if (Layer == LAYER_BASE_DIFFUSE)
        {
            OpenPBRBaseDiffuseBSDF(Surface, Lambda, Out, In, Path);
            Layer = In.z < 0 ? LAYER_EXTERNAL : LAYER_BASE_SPECULAR;
        }
        else if (Layer == LAYER_EXTERNAL)
        {
            break;
        }

        if (max4(Path.Weight) < EPSILON) break;

        Out = -In;
    }
}
