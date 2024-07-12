#pragma once

struct openpbr_material : material
{
    float    BaseWeight = 1.0f;
    vec3     BaseColor = { 1, 1, 1 };
    texture* BaseColorTexture = nullptr;
    float    BaseMetalness = 0.0f;
    float    BaseDiffuseRoughness = 0.0f;

    float    SpecularWeight = 1.0f;
    vec3     SpecularColor = { 1, 1, 1 };
    float    SpecularRoughness = 0.3f;
    texture* SpecularRoughnessTexture = nullptr;
    float    SpecularRoughnessAnisotropy = 0.0f;
    float    SpecularIOR = 1.5f;

    float    TransmissionWeight = 0.0f;
    vec3     TransmissionColor = { 1, 1, 1 };
    float    TransmissionDepth = 0.0f;
    vec3     TransmissionScatter = { 0, 0, 0 };
    float    TransmissionScatterAnisotropy = 0.0f;
    float    TransmissionDispersionScale = 0.0f;
    float    TransmissionDispersionAbbeNumber = 20.0f;

    float    CoatWeight = 0.0f;
    vec3     CoatColor = { 1, 1, 1 };
    float    CoatRoughness = 0.0f;
    float    CoatRoughnessAnisotropy = 0.0f;
    float    CoatIOR = 1.6f;
    float    CoatDarkening = 1.0f;

    float    EmissionLuminance = 0.0f;
    vec3     EmissionColor = { 0, 0, 0 };
    texture* EmissionColorTexture = nullptr;

    int      LayerBounceLimit = 16;

    uint32_t PackedMaterialIndex = 0;

    openpbr_material() { Type = MATERIAL_TYPE_OPENPBR; }
};

const uint OPENPBR_LAYER_BOUNCE_LIMIT                  = 1;
const uint OPENPBR_BASE_WEIGHT                         = 2;
const uint OPENPBR_BASE_SPECTRUM                       = 3;
const uint OPENPBR_BASE_SPECTRUM_TEXTURE_INDEX         = 6;
const uint OPENPBR_BASE_METALNESS                      = 7;
const uint OPENPBR_BASE_DIFFUSE_ROUGHNESS              = 8;
const uint OPENPBR_SPECULAR_WEIGHT                     = 9;
const uint OPENPBR_SPECULAR_SPECTRUM                   = 10;
const uint OPENPBR_SPECULAR_IOR                        = 13;
const uint OPENPBR_SPECULAR_ROUGHNESS                  = 14;
const uint OPENPBR_SPECULAR_ROUGHNESS_TEXTURE_INDEX    = 15;
const uint OPENPBR_SPECULAR_ROUGHNESS_ANISOTROPY       = 16;
const uint OPENPBR_TRANSMISSION_SPECTRUM               = 17;
const uint OPENPBR_TRANSMISSION_WEIGHT                 = 20;
const uint OPENPBR_TRANSMISSION_SCATTER_SPECTRUM       = 21;
const uint OPENPBR_TRANSMISSION_SCATTER_ANISOTROPY     = 24;
const uint OPENPBR_TRANSMISSION_DEPTH                  = 25;
const uint OPENPBR_TRANSMISSION_DISPERSION_ABBE_NUMBER = 26;
const uint OPENPBR_EMISSION_SPECTRUM                   = 27;
const uint OPENPBR_EMISSION_SPECTRUM_TEXTURE_INDEX     = 30;
const uint OPENPBR_EMISSION_LUMINANCE                  = 31;
const uint OPENPBR_COAT_WEIGHT                         = 32;
const uint OPENPBR_COAT_COLOR_SPECTRUM                 = 33;
const uint OPENPBR_COAT_IOR                            = 36;
const uint OPENPBR_COAT_ROUGHNESS                      = 37;
const uint OPENPBR_COAT_ROUGHNESS_ANISOTROPY           = 38;
const uint OPENPBR_COAT_DARKENING                      = 39;

template<typename function_type>
inline void OpenPBR_ForEachTexture(scene* Scene, openpbr_material* Material, function_type Function)
{
    Function(Material->BaseColorTexture);
    Function(Material->SpecularRoughnessTexture);
    Function(Material->EmissionColorTexture);
}

inline void OpenPBR_PackData(scene* Scene, openpbr_material* Material, uint* AttributeData)
{
    uint* A = AttributeData;

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

#ifdef IMGUI_IMPLEMENTATION

inline bool OpenPBR_Inspector(scene* Scene, openpbr_material* Material)
{
    bool C = false;

    C |= ImGui::DragFloat("Opacity", &Material->Opacity, 0.01f, 0.0f, 1.0f);

    C |= ImGui::DragFloat("Base Weight", &Material->BaseWeight, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Base Color", &Material->BaseColor[0]);
    C |= TextureSelectorDropDown("Base Color Texture", Scene, &Material->BaseColorTexture);
    C |= ImGui::DragFloat("Base Metalness", &Material->BaseMetalness, 0.01f, 0.0f, 1.0f);
    C |= ImGui::DragFloat("Base Diffuse Roughness", &Material->BaseDiffuseRoughness, 0.01f, 0.0f, 1.0f);

    C |= ImGui::DragFloat("Specular Weight", &Material->SpecularWeight, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Specular Color", &Material->SpecularColor[0]);
    C |= ImGui::DragFloat("Specular Roughness", &Material->SpecularRoughness, 0.01f, 0.0f, 1.0f);
    C |= TextureSelectorDropDown("Specular Roughness Texture", Scene, &Material->SpecularRoughnessTexture);
    C |= ImGui::DragFloat("Specular Roughness Anisotropy", &Material->SpecularRoughnessAnisotropy, 0.01f, 0.0f, 1.0f);
    C |= ImGui::DragFloat("Specular IOR", &Material->SpecularIOR, 0.01f, 1.0f, 3.0f);

    C |= ImGui::DragFloat("Transmission Weight", &Material->TransmissionWeight, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Transmission Color", &Material->TransmissionColor[0]);
    C |= ImGui::DragFloat("Transmission Depth", &Material->TransmissionDepth, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Transmission Scatter", &Material->TransmissionScatter[0]);
    C |= ImGui::DragFloat("Transmission Scatter Anisotropy", &Material->TransmissionScatterAnisotropy, 0.01f, -1.0f, 1.0f);
    C |= ImGui::DragFloat("Transmission Dispersion Scale", &Material->TransmissionDispersionScale, 0.01f, 0.0f, 1.0f);
    C |= ImGui::DragFloat("Transmission Dispersion Abbe Number", &Material->TransmissionDispersionAbbeNumber, 0.01f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

    C |= ImGui::DragFloat("Coat Weight", &Material->CoatWeight, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Coat Color", &Material->CoatColor[0]);
    C |= ImGui::DragFloat("Coat Roughness", &Material->CoatRoughness, 0.01f, 0.0f, 1.0f);
    C |= ImGui::DragFloat("Coat Roughness Anisotropy", &Material->CoatRoughnessAnisotropy, 0.01f, 0.0f, 1.0f);
    C |= ImGui::DragFloat("Coat IOR", &Material->CoatIOR, 0.01f, 1.0f, 3.0f);
    C |= ImGui::DragFloat("Coat Darkening", &Material->CoatDarkening, 0.01f, 0.0f, 1.0f);

    C |= ImGui::DragFloat("Emission Luminance", &Material->EmissionLuminance, 1.0f, 0.0f, 1000.0f);
    C |= ImGui::ColorEdit3("Emission Color", &Material->EmissionColor[0]);
    C |= TextureSelectorDropDown("Emission Color Texture", Scene, &Material->EmissionColorTexture);

    C |= ImGui::DragInt("Layer Bounce Limit", &Material->LayerBounceLimit, 1.0f, 1, 128);

    return C;
}

#endif

#ifdef SERIALIZER_IMPLEMENTATION

inline void OpenPBR_Serialize(serializer& S, json& JSON, openpbr_material& Material)
{
    #define F(NAME) Serialize(S, JSON[#NAME], Material.NAME);
    F(BaseWeight);
    F(BaseColor);
    F(BaseColorTexture);
    F(BaseMetalness);
    F(BaseDiffuseRoughness);
    F(SpecularWeight);
    F(SpecularColor);
    F(SpecularRoughness);
    F(SpecularRoughnessTexture);
    F(SpecularRoughnessAnisotropy);
    F(SpecularIOR);
    F(TransmissionWeight);
    F(TransmissionColor);
    F(TransmissionDepth);
    F(TransmissionScatter);
    F(TransmissionScatterAnisotropy);
    F(TransmissionDispersionScale);
    F(TransmissionDispersionAbbeNumber);
    F(CoatWeight);
    F(CoatColor);
    F(CoatRoughness);
    F(CoatRoughnessAnisotropy);
    F(CoatIOR);
    F(CoatDarkening);
    F(EmissionLuminance);
    F(EmissionColor);
    F(EmissionColorTexture);
    F(LayerBounceLimit);
    #undef F
}

#endif
