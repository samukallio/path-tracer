#pragma once

struct basic_translucent_material : material
{
    float    IOR = 1.5f;
    float    AbbeNumber = 20.0f;
    float    Roughness = 0.3f;
    texture* RoughnessTexture = nullptr;
    float    RoughnessAnisotropy = 0.0f;
    texture* RoughnessAnisotropyTexture = nullptr;
    vec3     TransmissionColor = { 1, 1, 1 };
    float    TransmissionDepth = 0.0f;
    vec3     ScatteringColor = { 1, 1, 1 };
    float    ScatteringAnisotropy = 0.0f;

    basic_translucent_material() { Type = MATERIAL_TYPE_BASIC_TRANSLUCENT; }
};

const uint BASIC_TRANSLUCENT_IOR                   = 1;
const uint BASIC_TRANSLUCENT_ABBE_NUMBER           = 2;
const uint BASIC_TRANSLUCENT_ROUGHNESS             = 3;
const uint BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY  = 5;
const uint BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM = 7;
const uint BASIC_TRANSLUCENT_TRANSMISSION_DEPTH    = 10;
const uint BASIC_TRANSLUCENT_SCATTERING_SPECTRUM   = 11;
const uint BASIC_TRANSLUCENT_SCATTERING_ANISOTROPY = 14;

template<typename function_type>
inline void BasicTranslucent_ForEachTexture(scene* Scene, basic_translucent_material* Material, function_type Function)
{
    Function(Material->RoughnessTexture);
    Function(Material->RoughnessAnisotropyTexture);
}

inline void BasicTranslucent_PackData(scene* Scene, basic_translucent_material* Material, uint* AttributeData)
{
    uint* A = AttributeData;

    A[BASIC_TRANSLUCENT_IOR] = glm::floatBitsToUint(Material->IOR);
    A[BASIC_TRANSLUCENT_ABBE_NUMBER] = glm::floatBitsToUint(Material->AbbeNumber);

    A[BASIC_TRANSLUCENT_ROUGHNESS+0] = glm::floatBitsToUint(Material->Roughness);
    A[BASIC_TRANSLUCENT_ROUGHNESS+1] = GetPackedTextureIndex(Material->RoughnessTexture);

    A[BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY+0] = glm::floatBitsToUint(Material->RoughnessAnisotropy);
    A[BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY+1] = GetPackedTextureIndex(Material->RoughnessAnisotropyTexture);

    vec3 TransmissionSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->TransmissionColor);
    A[BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM+0] = glm::floatBitsToUint(TransmissionSpectrum.x);
    A[BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM+1] = glm::floatBitsToUint(TransmissionSpectrum.y);
    A[BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM+2] = glm::floatBitsToUint(TransmissionSpectrum.z);

    A[BASIC_TRANSLUCENT_TRANSMISSION_DEPTH] = glm::floatBitsToInt(Material->TransmissionDepth);

    vec3 ScatteringSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->ScatteringColor);
    A[BASIC_TRANSLUCENT_SCATTERING_SPECTRUM+0] = glm::floatBitsToUint(ScatteringSpectrum.x);
    A[BASIC_TRANSLUCENT_SCATTERING_SPECTRUM+1] = glm::floatBitsToUint(ScatteringSpectrum.y);
    A[BASIC_TRANSLUCENT_SCATTERING_SPECTRUM+2] = glm::floatBitsToUint(ScatteringSpectrum.z);

    A[BASIC_TRANSLUCENT_SCATTERING_ANISOTROPY] = glm::floatBitsToInt(Material->ScatteringAnisotropy);
}
