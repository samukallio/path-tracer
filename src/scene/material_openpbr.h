#pragma once

struct material_openpbr : material
{
    float       BaseWeight                          = 1.0f;
    vec3        BaseColor                           = { 1, 1, 1 };
    texture*    BaseColorTexture                    = nullptr;
    float       BaseMetalness                       = 0.0f;
    float       BaseDiffuseRoughness                = 0.0f;

    float       SpecularWeight                      = 1.0f;
    vec3        SpecularColor                       = { 1, 1, 1 };
    float       SpecularRoughness                   = 0.3f;
    texture*    SpecularRoughnessTexture            = nullptr;
    float       SpecularRoughnessAnisotropy         = 0.0f;
    float       SpecularIOR                         = 1.5f;

    float       TransmissionWeight                  = 0.0f;
    vec3        TransmissionColor                   = { 1, 1, 1 };
    float       TransmissionDepth                   = 0.0f;
    vec3        TransmissionScatter                 = { 0, 0, 0 };
    float       TransmissionScatterAnisotropy       = 0.0f;
    float       TransmissionDispersionScale         = 0.0f;
    float       TransmissionDispersionAbbeNumber    = 20.0f;

    float       CoatWeight                          = 0.0f;
    vec3        CoatColor                           = { 1, 1, 1 };
    float       CoatRoughness                       = 0.0f;
    float       CoatRoughnessAnisotropy             = 0.0f;
    float       CoatIOR                             = 1.6f;
    float       CoatDarkening                       = 1.0f;

    float       EmissionLuminance                   = 0.0f;
    vec3        EmissionColor                       = { 0, 0, 0 };
    texture*    EmissionColorTexture                = nullptr;

    int         LayerBounceLimit                    = 16;

    uint32_t    PackedMaterialIndex                 = 0;

    material_openpbr() { Type = MATERIAL_TYPE_OPENPBR; }
};

const uint OPENPBR_OPACITY                                 = 0;
const uint OPENPBR_LAYER_BOUNCE_LIMIT                      = 1;
const uint OPENPBR_BASE_WEIGHT                             = 2;
const uint OPENPBR_BASE_SPECTRUM                           = 3;
const uint OPENPBR_BASE_SPECTRUM_TEXTURE_INDEX             = 6;
const uint OPENPBR_BASE_METALNESS                          = 7;
const uint OPENPBR_BASE_DIFFUSE_ROUGHNESS                  = 8;
const uint OPENPBR_SPECULAR_WEIGHT                         = 9;
const uint OPENPBR_SPECULAR_SPECTRUM                       = 10;
const uint OPENPBR_SPECULAR_IOR                            = 13;
const uint OPENPBR_SPECULAR_ROUGHNESS                      = 14;
const uint OPENPBR_SPECULAR_ROUGHNESS_TEXTURE_INDEX        = 15;
const uint OPENPBR_SPECULAR_ROUGHNESS_ANISOTROPY           = 16;
const uint OPENPBR_TRANSMISSION_SPECTRUM                   = 17;
const uint OPENPBR_TRANSMISSION_WEIGHT                     = 20;
const uint OPENPBR_TRANSMISSION_SCATTER_SPECTRUM           = 21;
const uint OPENPBR_TRANSMISSION_SCATTER_ANISOTROPY         = 24;
const uint OPENPBR_TRANSMISSION_DEPTH                      = 25;
const uint OPENPBR_TRANSMISSION_DISPERSION_ABBE_NUMBER     = 26;
const uint OPENPBR_EMISSION_SPECTRUM                       = 27;
const uint OPENPBR_EMISSION_SPECTRUM_TEXTURE_INDEX         = 30;
const uint OPENPBR_EMISSION_LUMINANCE                      = 31;
const uint OPENPBR_COAT_WEIGHT                             = 32;
const uint OPENPBR_COAT_COLOR_SPECTRUM                     = 33;
const uint OPENPBR_COAT_IOR                                = 36;
const uint OPENPBR_COAT_ROUGHNESS                          = 37;
const uint OPENPBR_COAT_ROUGHNESS_ANISOTROPY               = 38;
const uint OPENPBR_COAT_DARKENING                          = 39;

template<typename function_type>
inline void OpenPBRForEachTexture(material_openpbr* Material, function_type Function)
{
    Function(Material->BaseColorTexture);
    Function(Material->SpecularRoughnessTexture);
    Function(Material->EmissionColorTexture);
}

inline void OpenPBRPackMaterial(scene* Scene, material_openpbr* Material, uint* AttributeData)
{
    uint* A = AttributeData;

    A[OPENPBR_OPACITY] = glm::floatBitsToUint(Material->Opacity);
    A[OPENPBR_LAYER_BOUNCE_LIMIT] = static_cast<uint32_t>(Material->LayerBounceLimit);

    vec3 BaseSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->BaseColor);
    A[OPENPBR_BASE_WEIGHT] = glm::floatBitsToUint(Material->BaseWeight);
    A[OPENPBR_BASE_SPECTRUM+0] = glm::floatBitsToUint(BaseSpectrum.x);
    A[OPENPBR_BASE_SPECTRUM+1] = glm::floatBitsToUint(BaseSpectrum.y);
    A[OPENPBR_BASE_SPECTRUM+2] = glm::floatBitsToUint(BaseSpectrum.z);
    A[OPENPBR_BASE_SPECTRUM_TEXTURE_INDEX] = GetPackedTextureIndex(Material->BaseColorTexture);
    A[OPENPBR_BASE_METALNESS] = glm::floatBitsToUint(Material->BaseMetalness);
    A[OPENPBR_BASE_DIFFUSE_ROUGHNESS] = glm::floatBitsToUint(Material->BaseDiffuseRoughness);

    vec3 SpecularSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->SpecularColor);
    A[OPENPBR_SPECULAR_WEIGHT] = glm::floatBitsToUint(Material->SpecularWeight);
    A[OPENPBR_SPECULAR_SPECTRUM+0] = glm::floatBitsToUint(SpecularSpectrum.x);
    A[OPENPBR_SPECULAR_SPECTRUM+1] = glm::floatBitsToUint(SpecularSpectrum.y);
    A[OPENPBR_SPECULAR_SPECTRUM+2] = glm::floatBitsToUint(SpecularSpectrum.z);
    A[OPENPBR_SPECULAR_IOR] = glm::floatBitsToUint(Material->SpecularIOR);
    A[OPENPBR_SPECULAR_ROUGHNESS] = glm::floatBitsToUint(Material->SpecularRoughness);
    A[OPENPBR_SPECULAR_ROUGHNESS_TEXTURE_INDEX] = GetPackedTextureIndex(Material->SpecularRoughnessTexture);
    A[OPENPBR_SPECULAR_ROUGHNESS_ANISOTROPY] = glm::floatBitsToUint(Material->SpecularRoughnessAnisotropy);

    vec3 TransmissionSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->TransmissionColor);
    vec3 TransmissionScatterSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->TransmissionScatter);
    A[OPENPBR_TRANSMISSION_WEIGHT] = glm::floatBitsToUint(Material->TransmissionWeight);
    A[OPENPBR_TRANSMISSION_SPECTRUM+0] = glm::floatBitsToUint(TransmissionSpectrum.x);
    A[OPENPBR_TRANSMISSION_SPECTRUM+1] = glm::floatBitsToUint(TransmissionSpectrum.y);
    A[OPENPBR_TRANSMISSION_SPECTRUM+2] = glm::floatBitsToUint(TransmissionSpectrum.z);
    A[OPENPBR_TRANSMISSION_DEPTH] = glm::floatBitsToUint(Material->TransmissionDepth);
    A[OPENPBR_TRANSMISSION_SCATTER_SPECTRUM+0] = glm::floatBitsToUint(TransmissionScatterSpectrum.x);
    A[OPENPBR_TRANSMISSION_SCATTER_SPECTRUM+1] = glm::floatBitsToUint(TransmissionScatterSpectrum.y);
    A[OPENPBR_TRANSMISSION_SCATTER_SPECTRUM+2] = glm::floatBitsToUint(TransmissionScatterSpectrum.z);
    A[OPENPBR_TRANSMISSION_SCATTER_ANISOTROPY] = glm::floatBitsToUint(Material->TransmissionScatterAnisotropy);
    A[OPENPBR_TRANSMISSION_DISPERSION_ABBE_NUMBER] = glm::floatBitsToUint(Material->TransmissionDispersionAbbeNumber / Material->TransmissionDispersionScale);

    vec3 CoatColorSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->CoatColor);
    A[OPENPBR_COAT_WEIGHT] = glm::floatBitsToUint(Material->CoatWeight);
    A[OPENPBR_COAT_COLOR_SPECTRUM+0] = glm::floatBitsToUint(CoatColorSpectrum.x);
    A[OPENPBR_COAT_COLOR_SPECTRUM+1] = glm::floatBitsToUint(CoatColorSpectrum.y);
    A[OPENPBR_COAT_COLOR_SPECTRUM+2] = glm::floatBitsToUint(CoatColorSpectrum.z);
    A[OPENPBR_COAT_ROUGHNESS] = glm::floatBitsToInt(Material->CoatRoughness);
    A[OPENPBR_COAT_ROUGHNESS_ANISOTROPY] = glm::floatBitsToInt(Material->CoatRoughnessAnisotropy);
    A[OPENPBR_COAT_IOR] = glm::floatBitsToUint(Material->CoatIOR);
    A[OPENPBR_COAT_DARKENING] = glm::floatBitsToUint(Material->CoatDarkening);

    vec3 EmissionSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->EmissionColor);
    A[OPENPBR_EMISSION_SPECTRUM+0] = glm::floatBitsToUint(EmissionSpectrum.x);
    A[OPENPBR_EMISSION_SPECTRUM+1] = glm::floatBitsToUint(EmissionSpectrum.y);
    A[OPENPBR_EMISSION_SPECTRUM+2] = glm::floatBitsToUint(EmissionSpectrum.z);
    A[OPENPBR_EMISSION_SPECTRUM_TEXTURE_INDEX] = GetPackedTextureIndex(Material->EmissionColorTexture);
    A[OPENPBR_EMISSION_LUMINANCE] = glm::floatBitsToUint(Material->EmissionLuminance);
}
