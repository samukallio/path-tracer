#pragma once

struct basic_diffuse_material : material
{
    vec3     BaseColor = { 1, 1, 1 };
    texture* BaseTexture = nullptr;

    basic_diffuse_material() { Type = MATERIAL_TYPE_BASIC_DIFFUSE; }
};

template<typename function_type>
inline void BasicDiffuse_ForEachTexture(scene* Scene, basic_diffuse_material* Material, function_type&& Function)
{
    Function(Material->BaseTexture);
}

inline void BasicDiffuse_PackData(scene* Scene, basic_diffuse_material* Material, uint* AttributeData)
{
    const uint BASIC_DIFFUSE_BASE_SPECTRUM = 1;

    uint* A = AttributeData;

    vec3 BaseSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->BaseColor);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+0] = glm::floatBitsToUint(BaseSpectrum.x);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+1] = glm::floatBitsToUint(BaseSpectrum.y);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+2] = glm::floatBitsToUint(BaseSpectrum.z);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+3] = GetPackedTextureIndex(Material->BaseTexture);
}

#ifdef IMGUI_IMPLEMENTATION

inline bool BasicDiffuse_Inspector(scene* Scene, basic_diffuse_material* Material)
{
    bool C = false;

    C |= ImGui::ColorEdit3("Base Color", &Material->BaseColor[0]);
    C |= TextureSelectorDropDown("Base Color Texture", Scene, &Material->BaseTexture);

    return C;
}

#endif

#ifdef SERIALIZER_IMPLEMENTATION

inline void BasicDiffuse_Serialize(serializer& S, json& JSON, basic_diffuse_material& Material)
{
    #define F(NAME) Serialize(S, JSON[#NAME], Material.NAME);
    F(BaseColor);
    F(BaseTexture);
    #undef F
}

#endif