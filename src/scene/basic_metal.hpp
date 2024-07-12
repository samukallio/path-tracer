#pragma once

struct basic_metal_material : material
{
    vec3     BaseColor = { 1, 1, 1 };
    texture* BaseTexture = nullptr;
    vec3     SpecularColor = { 1, 1, 1 };
    texture* SpecularTexture = nullptr;
    float    Roughness = 0.3f;
    texture* RoughnessTexture = nullptr;
    float    RoughnessAnisotropy = 0.0f;
    texture* RoughnessAnisotropyTexture = nullptr;

    basic_metal_material() { Type = MATERIAL_TYPE_BASIC_METAL; }
};

const uint BASIC_METAL_BASE_SPECTRUM        = 1;
const uint BASIC_METAL_SPECULAR_SPECTRUM    = 5;
const uint BASIC_METAL_ROUGHNESS            = 9;
const uint BASIC_METAL_ROUGHNESS_ANISOTROPY = 11;

template<typename function_type>
inline void BasicMetal_ForEachTexture(scene* Scene, basic_metal_material* Material, function_type&& Function)
{
    Function(Material->BaseTexture);
    Function(Material->SpecularTexture);
    Function(Material->RoughnessTexture);
    Function(Material->RoughnessAnisotropyTexture);
}

inline void BasicMetal_PackData(scene* Scene, basic_metal_material* Material, uint* AttributeData)
{
    uint* A = AttributeData;

    vec3 BaseSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->BaseColor);
    A[BASIC_METAL_BASE_SPECTRUM+0] = glm::floatBitsToUint(BaseSpectrum.x);
    A[BASIC_METAL_BASE_SPECTRUM+1] = glm::floatBitsToUint(BaseSpectrum.y);
    A[BASIC_METAL_BASE_SPECTRUM+2] = glm::floatBitsToUint(BaseSpectrum.z);
    A[BASIC_METAL_BASE_SPECTRUM+3] = GetPackedTextureIndex(Material->BaseTexture);

    vec3 SpecularSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->SpecularColor);
    A[BASIC_METAL_SPECULAR_SPECTRUM+0] = glm::floatBitsToUint(SpecularSpectrum.x);
    A[BASIC_METAL_SPECULAR_SPECTRUM+1] = glm::floatBitsToUint(SpecularSpectrum.y);
    A[BASIC_METAL_SPECULAR_SPECTRUM+2] = glm::floatBitsToUint(SpecularSpectrum.z);
    A[BASIC_METAL_SPECULAR_SPECTRUM+3] = GetPackedTextureIndex(Material->SpecularTexture);

    A[BASIC_METAL_ROUGHNESS+0] = glm::floatBitsToUint(Material->Roughness);
    A[BASIC_METAL_ROUGHNESS+1] = GetPackedTextureIndex(Material->RoughnessTexture);

    A[BASIC_METAL_ROUGHNESS_ANISOTROPY+0] = glm::floatBitsToUint(Material->RoughnessAnisotropy);
    A[BASIC_METAL_ROUGHNESS_ANISOTROPY+1] = GetPackedTextureIndex(Material->RoughnessAnisotropyTexture);
}

#ifdef IMGUI_IMPLEMENTATION

inline bool BasicMetal_Inspector(scene* Scene, basic_metal_material* Material)
{
    bool C = false;

    C |= ImGui::ColorEdit3("Base Color", &Material->BaseColor[0]);
    C |= TextureSelectorDropDown("Base Color Texture", Scene, &Material->BaseTexture);
    C |= ImGui::ColorEdit3("Specular Color", &Material->SpecularColor[0]);
    C |= TextureSelectorDropDown("Specular Color Texture", Scene, &Material->SpecularTexture);
    C |= ImGui::DragFloat("Roughness", &Material->Roughness, 0.01f, 0.0f, 1.0f);
    C |= TextureSelectorDropDown("Roughness Texture", Scene, &Material->RoughnessTexture);
    C |= ImGui::DragFloat("Roughness Anisotropy", &Material->RoughnessAnisotropy, 0.01f, 0.0f, 1.0f);
    C |= TextureSelectorDropDown("Roughness Anisotropy Texture", Scene, &Material->RoughnessAnisotropyTexture);

    return C;
}

#endif

#ifdef SERIALIZER_IMPLEMENTATION

inline void BasicMetal_Serialize(serializer& S, json& JSON, basic_metal_material& Material)
{
    #define F(NAME) Serialize(S, JSON[#NAME], Material.NAME);
    F(BaseColor);
    F(BaseTexture);
    F(SpecularColor);
    F(SpecularTexture);
    F(Roughness);
    F(RoughnessTexture);
    F(RoughnessAnisotropy);
    F(RoughnessAnisotropyTexture);
    #undef F
}

#endif