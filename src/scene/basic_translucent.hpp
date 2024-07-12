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

template<typename function_type>
inline void BasicTranslucent_ForEachTexture(scene* Scene, basic_translucent_material* Material, function_type&& Function)
{
    Function(Material->RoughnessTexture);
    Function(Material->RoughnessAnisotropyTexture);
}

inline void BasicTranslucent_PackData(scene* Scene, basic_translucent_material* Material, uint* AttributeData)
{
    const uint BASIC_TRANSLUCENT_IOR                   = 1;
    const uint BASIC_TRANSLUCENT_ABBE_NUMBER           = 2;
    const uint BASIC_TRANSLUCENT_ROUGHNESS             = 3;
    const uint BASIC_TRANSLUCENT_ROUGHNESS_ANISOTROPY  = 5;
    const uint BASIC_TRANSLUCENT_TRANSMISSION_SPECTRUM = 7;
    const uint BASIC_TRANSLUCENT_TRANSMISSION_DEPTH    = 10;
    const uint BASIC_TRANSLUCENT_SCATTERING_SPECTRUM   = 11;
    const uint BASIC_TRANSLUCENT_SCATTERING_ANISOTROPY = 14;

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

#ifdef IMGUI_IMPLEMENTATION

inline bool BasicTranslucent_Inspector(scene* Scene, basic_translucent_material* Material)
{
    bool C = false;

    C |= ImGui::DragFloat("IOR", &Material->IOR, 0.01f, 1.0f, 3.0f);
    C |= ImGui::DragFloat("Abbe Number", &Material->AbbeNumber, 1.0f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

    C |= ImGui::DragFloat("Roughness", &Material->Roughness, 0.01f, 0.0f, 1.0f);
    C |= TextureSelectorDropDown("Roughness Texture", Scene, &Material->RoughnessTexture);
    C |= ImGui::DragFloat("Roughness Anisotropy", &Material->RoughnessAnisotropy, 0.01f, 0.0f, 1.0f);
    C |= TextureSelectorDropDown("Roughness Anisotropy Texture", Scene, &Material->RoughnessAnisotropyTexture);

    C |= ImGui::ColorEdit3("Transmission Color", &Material->TransmissionColor[0]);
    C |= ImGui::DragFloat("Transmission Depth", &Material->TransmissionDepth, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Scattering Color", &Material->ScatteringColor[0]);
    C |= ImGui::DragFloat("Scattering Anisotropy", &Material->ScatteringAnisotropy, 0.01f, -1.0f, 1.0f);

    return C;
}

#endif

#ifdef SERIALIZER_IMPLEMENTATION

inline void BasicTranslucent_Serialize(serializer& S, json& JSON, basic_translucent_material& Material)
{
    #define F(NAME) Serialize(S, JSON[#NAME], Material.NAME);
    F(IOR);
    F(AbbeNumber);
    F(Roughness);
    F(RoughnessTexture);
    F(RoughnessAnisotropy);
    F(RoughnessAnisotropyTexture);
    F(TransmissionColor);
    F(TransmissionDepth);
    F(ScatteringColor);
    F(ScatteringAnisotropy);
    #undef F
}

#endif
