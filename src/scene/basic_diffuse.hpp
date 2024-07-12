#pragma once

struct basic_diffuse_material : material
{
    vec3     BaseColor = { 1, 1, 1 };
    texture* BaseTexture = nullptr;

    basic_diffuse_material() { Type = MATERIAL_TYPE_BASIC_DIFFUSE; }
};

const uint BASIC_DIFFUSE_BASE_SPECTRUM = 1;

template<typename function_type>
inline void BasicDiffuse_ForEachTexture(scene* Scene, basic_diffuse_material* Material, function_type Function)
{
    Function(Material->BaseTexture);
}

inline void BasicDiffuse_PackData(scene* Scene, basic_diffuse_material* Material, uint* AttributeData)
{
    uint* A = AttributeData;

    vec3 BaseSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->BaseColor);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+0] = glm::floatBitsToUint(BaseSpectrum.x);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+1] = glm::floatBitsToUint(BaseSpectrum.y);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+2] = glm::floatBitsToUint(BaseSpectrum.z);
    A[BASIC_DIFFUSE_BASE_SPECTRUM+3] = GetPackedTextureIndex(Material->BaseTexture);
}
