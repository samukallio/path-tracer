#include "common.h"
#include "ui.h"
#include "spectral.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

static bool DragEulerAngles(const char* Label, glm::vec3* Angles)
{
    float Degrees[3];
    for (int I = 0; I < 3; I++)
        Degrees[I] = RepeatRange((*Angles)[I], -PI, +PI) * (360 / TAU);
    bool Changed = ImGui::DragFloat3(Label, Degrees);
    for (int I = 0; I < 3; I++)
        (*Angles)[I] = RepeatRange(Degrees[I] * (TAU / 360), -PI, +PI);
    return Changed;
}

template<typename resource_t>
static bool ResourceSelectorDropDown(
    char const* Label,
    std::vector<resource_t*>& Resources,
    resource_t** ResourcePtr)
{
    auto GetItemName = [](void* Context, int Index) {
        auto& Resources = *static_cast<std::vector<resource_t*>*>(Context);
        if (Index <= 0)
            return "(none)";
        if (Index > Resources.size())
            return "";
        return Resources[Index-1]->Name.c_str();
    };

    int Index = 0;
    int Count = static_cast<int>(Resources.size()) + 1;

    for (int I = 0; I < Count-1; I++)
        if (Resources[I] == *ResourcePtr)
            Index = I + 1;

    bool Changed = ImGui::Combo(Label, &Index, GetItemName, &Resources, Count, 6);

    if (Changed) {
        if (Index == 0)
            *ResourcePtr = nullptr;
        else
            *ResourcePtr = Resources[Index-1];
    }

    return Changed;
}

static void TextureInspector(application* App, texture* Texture)
{
    scene* Scene = App->Scene;

    if (!Texture) return;

    ImGui::PushID(Texture);

    ImGui::SeparatorText("Texture");

    bool C = false;

    ImGui::InputText("Name", &Texture->Name);
    ImGui::LabelText("Size", "%u x %u", Texture->Width, Texture->Height);

    if (ImGui::BeginCombo("Type", TextureTypeName(Texture->Type))) {
        for (int I = 0; I < TEXTURE_TYPE__COUNT; I++) {
            auto Type = static_cast<texture_type>(I);
            bool IsSelected = Texture->Type == Type;
            if (ImGui::Selectable(TextureTypeName(Type), &IsSelected)) {
                Texture->Type = Type;
                C = true;
            }
        }
        ImGui::EndCombo();
    }

    C |= ImGui::Checkbox("Nearest Filtering", &Texture->EnableNearestFiltering);

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_TEXTURES;

    ImGui::PopID();
}

static void MaterialInspector(application* App, material* Material, bool Referenced = false)
{
    scene* Scene = App->Scene;

    if (!Material) return;

    ImGui::PushID(Material);

    if (Referenced) {
        char Title[256];
        snprintf(Title, 256, "Material: %s", Material->Name.c_str());
        ImGui::SeparatorText(Title);
    }
    else {
        ImGui::SeparatorText("Material");
        ImGui::InputText("Name", &Material->Name);
    }

    bool C = false;

    C |= ImGui::DragFloat("Opacity", &Material->Opacity, 0.01f, 0.0f, 1.0f);

    C |= ImGui::DragFloat("Base Weight", &Material->BaseWeight, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Base Color", &Material->BaseColor[0]);
    C |= ResourceSelectorDropDown("Base Color Texture", Scene->Textures, &Material->BaseColorTexture);
    C |= ImGui::DragFloat("Base Metalness", &Material->BaseMetalness, 0.01f, 0.0f, 1.0f);
    C |= ImGui::DragFloat("Base Diffuse Roughness", &Material->BaseDiffuseRoughness, 0.01f, 0.0f, 1.0f);

    C |= ImGui::DragFloat("Specular Weight", &Material->SpecularWeight, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Specular Color", &Material->SpecularColor[0]);
    C |= ImGui::DragFloat("Specular Roughness", &Material->SpecularRoughness, 0.01f, 0.0f, 1.0f);
    C |= ResourceSelectorDropDown("Specular Roughness Texture", Scene->Textures, &Material->SpecularRoughnessTexture);
    C |= ImGui::DragFloat("Specular Roughness Anisotropy", &Material->SpecularRoughnessAnisotropy, 0.01f, 0.0f, 1.0f);
    C |= ImGui::DragFloat("Specular IOR", &Material->SpecularIOR, 0.01f, 1.0f, 3.0f);

    C |= ImGui::DragFloat("Transmission Weight", &Material->TransmissionWeight, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Transmission Color", &Material->TransmissionColor[0]);
    C |= ImGui::DragFloat("Transmission Depth", &Material->TransmissionDepth, 0.01f, 0.0f, 1.0f);
    C |= ImGui::ColorEdit3("Transmission Scatter", &Material->TransmissionScatter[0]);
    C |= ImGui::DragFloat("Transmission Scatter Anisotropy", &Material->TransmissionScatterAnisotropy, 0.01f, 0.0f, 1.0f);
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

    C |= ImGui::DragFloat("Scattering Rate", &Material->ScatteringRate, 1.0f, 0.0001f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

    C |= ImGui::DragInt("Layer Bounce Limit", &Material->LayerBounceLimit, 1.0f, 1, 128);

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_MATERIALS;

    ImGui::PopID();
}

static void MeshInspector(application* App, mesh* Mesh, bool Referenced = false)
{
    scene* Scene = App->Scene;

    if (!Mesh) return;

    ImGui::PushID(Mesh);

    if (Referenced) {
        char Title[256];
        snprintf(Title, 256, "Mesh: %s", Mesh->Name.c_str());
        ImGui::SeparatorText(Title);
    }
    else {
        ImGui::SeparatorText("Mesh");
        ImGui::InputText("Name", &Mesh->Name);
    }

    bool C = false;

    ImGui::PushID("materials");
    for (size_t I = 0; I < Mesh->Materials.size(); I++) {
        ImGui::PushID(static_cast<int>(I));

        char Title[32];
        sprintf_s(Title, "Material %llu", I);

        C |= ResourceSelectorDropDown(Title, Scene->Materials, &Mesh->Materials[I]);

        ImGui::PopID();
    }
    ImGui::PopID();

    for (size_t I = 0; I < Mesh->Materials.size(); I++) {
        ImGui::Spacing();
        MaterialInspector(App, Mesh->Materials[I], true);
    }

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_MESHES;

    ImGui::PopID();
}

static void CameraInspector(application* App, camera* Camera)
{
    bool C = false;

    if (App->Camera == Camera) {
        bool Active = true;
        ImGui::Checkbox("Possess", &Active);
        if (!Active) App->Camera = nullptr;
    }
    else {
        bool Active = false;
        ImGui::Checkbox("Possess", &Active);
        if (Active) App->Camera = Camera;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Rendering");

    if (ImGui::BeginCombo("Render Mode", RenderModeName(Camera->RenderMode))) {
        for (int I = 0; I < RENDER_MODE__COUNT; I++) {
            auto RenderMode = static_cast<render_mode>(I);
            bool IsSelected = Camera->RenderMode == RenderMode;
            if (ImGui::Selectable(RenderModeName(RenderMode), &IsSelected)) {
                Camera->RenderMode = RenderMode;
                C = true;
            }
        }
        ImGui::EndCombo();
    }
    
    if (Camera->RenderMode == RENDER_MODE_PATH_TRACE) {
        int BounceLimit = static_cast<int>(Camera->RenderBounceLimit);
        C |= ImGui::InputInt("Bounce Limit", &BounceLimit);
        C |= ImGui::DragFloat("Termination Probability", &Camera->RenderTerminationProbability, 0.001f, 0.0f, 1.0f);
        Camera->RenderBounceLimit = std::max(1, BounceLimit);
    }

    if (Camera->RenderMode == RENDER_MODE_MESH_COMPLEXITY) {
        int ComplexityScale = static_cast<int>(Camera->RenderMeshComplexityScale);
        C |= ImGui::InputInt("Maximum Complexity", &ComplexityScale);
        Camera->RenderMeshComplexityScale = std::max(1, ComplexityScale);
    }

    if (Camera->RenderMode == RENDER_MODE_SCENE_COMPLEXITY) {
        int ComplexityScale = static_cast<int>(Camera->RenderSceneComplexityScale);
        C |= ImGui::InputInt("Maximum Complexity", &ComplexityScale);
        Camera->RenderSceneComplexityScale = std::max(1, ComplexityScale);
    }

    char const* const RenderSampleBlockSizeLabels[] = { "1x1", "2x2", "4x4", "8x8" };
    ImGui::Combo("Sample Block Size",
        (int*)&Camera->RenderSampleBlockSizeLog2,
        RenderSampleBlockSizeLabels, 4);

    C |= ImGui::CheckboxFlags("Sample Accumulation", &Camera->RenderFlags, RENDER_FLAG_ACCUMULATE);
    C |= ImGui::CheckboxFlags("Sample Jitter", &Camera->RenderFlags, RENDER_FLAG_SAMPLE_JITTER);

    ImGui::Spacing();
    ImGui::SeparatorText("Post-Processing");

    ImGui::SliderFloat("Brightness", &Camera->Brightness, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

    if (ImGui::BeginCombo("Tone Mapping", ToneMappingModeName(Camera->ToneMappingMode))) {
        for (int I = 0; I < TONE_MAPPING_MODE__COUNT; I++) {
            auto ToneMappingMode = static_cast<tone_mapping_mode>(I);
            bool IsSelected = Camera->ToneMappingMode == ToneMappingMode;
            if (ImGui::Selectable(ToneMappingModeName(ToneMappingMode), &IsSelected)) {
                Camera->ToneMappingMode = ToneMappingMode;
            }
        }
        ImGui::EndCombo();
    }

    if (Camera->ToneMappingMode == TONE_MAPPING_MODE_REINHARD) {
        ImGui::SliderFloat("White Level", &Camera->ToneMappingWhiteLevel, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Projection");

    if (ImGui::BeginCombo("Camera Model", CameraModelName(Camera->CameraModel))) {
        for (int I = 0; I < CAMERA_MODEL__COUNT; I++) {
            auto CameraModel = static_cast<camera_model>(I);
            bool IsSelected = Camera->CameraModel == CameraModel;
            if (ImGui::Selectable(CameraModelName(CameraModel), &IsSelected)) {
                Camera->CameraModel = CameraModel;
                C = true;
            }
        }
        ImGui::EndCombo();
    }

    if (Camera->CameraModel == CAMERA_MODEL_PINHOLE) {
        C |= ImGui::DragFloat("FOV (degrees)", &Camera->Pinhole.FieldOfViewInDegrees, 1.0f, 0.01f, 179.99f);
        C |= ImGui::DragFloat("Aperture (mm)", &Camera->Pinhole.ApertureDiameterInMM, 0.1f, 0.0f, 50.0f);
    }

    if (Camera->CameraModel == CAMERA_MODEL_THIN_LENS) {
        glm::vec2 SensorSizeInMM = Camera->ThinLens.SensorSizeInMM;
        if (ImGui::DragFloat2("Sensor Size (mm)", &SensorSizeInMM[0], 1.0f, 1.0f, 100.0f)) {
            float const ASPECT_RATIO = 1920.0f / 1080.0f;
            if (SensorSizeInMM.x != Camera->ThinLens.SensorSizeInMM.x)
                SensorSizeInMM.y = SensorSizeInMM.x / ASPECT_RATIO;
            else
                SensorSizeInMM.x = SensorSizeInMM.y * ASPECT_RATIO;
            Camera->ThinLens.SensorSizeInMM = SensorSizeInMM;
            C = true;
        }

        C |= ImGui::DragFloat("Focal Length (mm)", &Camera->ThinLens.FocalLengthInMM, 1.0f, 1.0f, 200.0f);
        C |= ImGui::DragFloat("Aperture (mm)", &Camera->ThinLens.ApertureDiameterInMM, 0.5f, 0.0f, 100.0f);
        C |= ImGui::DragFloat("Focus Distance", &Camera->ThinLens.FocusDistance, 1.0f, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    if (C) App->Scene->DirtyFlags |= SCENE_DIRTY_CAMERAS;
}

static void EntityInspector(application* App, entity* Entity)
{
    scene* Scene = App->Scene;

    if (!Entity) return;

    ImGui::PushID(Entity);
    ImGui::SeparatorText(EntityTypeName(Entity->Type));

    bool C = false;

    if (Entity->Type != ENTITY_TYPE_ROOT) {
        C |= ImGui::Checkbox("Active", &Entity->Active);

        ImGui::InputText("Name", &Entity->Name);

        transform& Transform = Entity->Transform;
        C |= ImGui::DragFloat3("Position", &Transform.Position[0], 0.1f);
        C |= DragEulerAngles("Rotation", &Transform.Rotation);

        if (Entity->Type != ENTITY_TYPE_CAMERA) {
            glm::vec3 Scale = Transform.Scale;
            if (ImGui::DragFloat3("Scale", &Scale[0], 0.01f)) {
                if (Transform.ScaleIsUniform) {
                    for (int I = 0; I < 3; I++) {
                        if (Scale[I] == Transform.Scale[I])
                            continue;
                        Scale = glm::vec3(1) * Scale[I];
                        break;
                    }
                }
                C = true;
            }
            if (ImGui::Checkbox("Uniform Scale", &Transform.ScaleIsUniform)) {
                if (Transform.ScaleIsUniform)
                    Scale = glm::vec3(1) * Scale.x;
                C = true;
            }
            Transform.Scale = Scale;
        }
    }

    switch (Entity->Type) {
        case ENTITY_TYPE_ROOT: {
            auto Root = static_cast<root*>(Entity);
            C |= ImGui::DragFloat("Scattering Rate", &Root->ScatterRate, 0.001f, 0.00001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            C |= ImGui::DragFloat("Skybox Brightness", &Root->SkyboxBrightness, 0.01f, 0.0f, 1.0f);
            C |= ResourceSelectorDropDown("Skybox Texture", Scene->Textures, &Root->SkyboxTexture);
            break;
        }
        case ENTITY_TYPE_CAMERA: {
            CameraInspector(App, static_cast<camera*>(Entity));
            break;
        }
        case ENTITY_TYPE_MESH_INSTANCE: {
            auto Instance = static_cast<mesh_instance*>(Entity);
            C |= ResourceSelectorDropDown("Mesh", Scene->Meshes, &Instance->Mesh);
            ImGui::Spacing();
            MeshInspector(App, Instance->Mesh, true);
            break;
        }
        case ENTITY_TYPE_PLANE: {
            auto Plane = static_cast<plane*>(Entity);
            C |= ResourceSelectorDropDown("Material", Scene->Materials, &Plane->Material);
            ImGui::Spacing();
            MaterialInspector(App, Plane->Material, true);
            break;
        }
        case ENTITY_TYPE_SPHERE: {
            auto Sphere = static_cast<sphere*>(Entity);
            C |= ResourceSelectorDropDown("Material", Scene->Materials, &Sphere->Material);
            ImGui::Spacing();
            MaterialInspector(App, Sphere->Material, true);
            break;
        }
        case ENTITY_TYPE_CUBE: {
            auto Cube = static_cast<cube*>(Entity);
            C |= ResourceSelectorDropDown("Material", Scene->Materials, &Cube->Material);
            ImGui::Spacing();
            MaterialInspector(App, Cube->Material, true);
            break;
        }
    }

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;

    ImGui::PopID();
}

static void EntityTreeNode(application* App, entity* Entity)
{
    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (Entity->Children.empty())
        Flags |= ImGuiTreeNodeFlags_Leaf;

    if (Entity->Type == ENTITY_TYPE_ROOT)
        Flags |= ImGuiTreeNodeFlags_DefaultOpen;

    if (App->SelectionType == SELECTION_TYPE_ENTITY && App->SelectedEntity == Entity)
        Flags |= ImGuiTreeNodeFlags_Selected;

    if (!Entity->Active) {
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.x *= 0.5f;
        color.y *= 0.5f;
        color.z *= 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
    }

    if (ImGui::TreeNodeEx(Entity->Name.c_str(), Flags)) {
        if (ImGui::IsItemClicked()) {
            App->SelectionType = SELECTION_TYPE_ENTITY;
            App->SelectedEntity = Entity;
        }

        if (ImGui::BeginPopupContextItem()) {
            for (int I = 0; I < ENTITY_TYPE__COUNT; I++) {
                if (I == ENTITY_TYPE_ROOT) continue;
                char Buffer[256];
                auto EntityType = static_cast<entity_type>(I);
                snprintf(Buffer, std::size(Buffer), "Create %s...", EntityTypeName(EntityType));
                if (ImGui::MenuItem(Buffer)) {
                    auto Child = CreateEntity(App->Scene, EntityType, Entity);
                    Child->Name = std::format("New {}", EntityTypeName(EntityType));
                    App->Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
                    App->SelectionType = SELECTION_TYPE_ENTITY;
                    App->SelectedEntity = Child;
                }
            }
            ImGui::EndPopup();
        }

        for (entity* Child : Entity->Children)
            EntityTreeNode(App, Child);

        ImGui::TreePop();
    }

    if (!Entity->Active) {
        ImGui::PopStyleColor();
    }
}

void ResourceBrowserWindow(application* App)
{
    bool C = false;

    scene* Scene = App->Scene;

    ImGui::Begin("Resources");

    // Textures
    {
        auto GetItemName = [](void* Context, int Index) {
            auto Scene = static_cast<scene*>(Context);
            if (Index < 0 || Index >= Scene->Textures.size())
                return "";
            return Scene->Textures[Index]->Name.c_str();
        };

        int Index = -1;
        int Count = static_cast<int>(Scene->Textures.size());

        if (App->SelectionType == SELECTION_TYPE_TEXTURE) {
            for (int I = 0; I < Count; I++)
                if (Scene->Textures[I] == App->SelectedTexture)
                    Index = I;
        }

        if (ImGui::ListBox("Textures", &Index, GetItemName, Scene, Count, 6)) {
            App->SelectionType = SELECTION_TYPE_TEXTURE;
            App->SelectedTexture = Scene->Textures[Index];
        }
    }

    // Materials
    {
        auto GetItemName = [](void* Context, int Index) {
            auto Scene = static_cast<scene*>(Context);
            if (Index < 0 || Index >= Scene->Materials.size())
                return "";
            return Scene->Materials[Index]->Name.c_str();
        };

        int Index = -1;
        int Count = static_cast<int>(Scene->Materials.size());

        if (App->SelectionType == SELECTION_TYPE_MATERIAL) {
            for (int I = 0; I < Count; I++)
                if (Scene->Materials[I] == App->SelectedMaterial)
                    Index = I;
        }

        if (ImGui::ListBox("Materials", &Index, GetItemName, Scene, Count, 6)) {
            App->SelectionType = SELECTION_TYPE_MATERIAL;
            App->SelectedMaterial = Scene->Materials[Index];
        }
    }

    // Meshes
    {
        auto GetItemName = [](void* Context, int Index) {
            auto Scene = static_cast<scene*>(Context);
            if (Index < 0 || Index >= Scene->Meshes.size())
                return "";
            return Scene->Meshes[Index]->Name.c_str();
        };

        int Index = -1;
        int Count = static_cast<int>(Scene->Meshes.size());

        if (App->SelectionType == SELECTION_TYPE_MESH) {
            for (int I = 0; I < Count; I++)
                if (Scene->Meshes[I] == App->SelectedMesh)
                    Index = I;
        }

        if (ImGui::ListBox("Meshes", &Index, GetItemName, Scene, Count, 6)) {
            App->SelectionType = SELECTION_TYPE_MESH;
            App->SelectedMesh = Scene->Meshes[Index];
        }
    }

    ImGui::End();
}

void SceneHierarchyWindow(application* App)
{
    scene* Scene = App->Scene;

    ImGui::Begin("Scene Hierarchy");

    EntityTreeNode(App, &Scene->Root);

    ImGui::End();
}

void InspectorWindow(application* App)
{
    ImGui::Begin("Inspector");
    ImGui::PushItemWidth(0.50f * ImGui::GetWindowWidth());

    scene* Scene = App->Scene;

    switch (App->SelectionType) {
        case SELECTION_TYPE_TEXTURE:
            TextureInspector(App, App->SelectedTexture);
            break;
        case SELECTION_TYPE_MATERIAL:
            MaterialInspector(App, App->SelectedMaterial);
            break;
        case SELECTION_TYPE_MESH:
            MeshInspector(App, App->SelectedMesh);
            break;
        case SELECTION_TYPE_ENTITY:
            EntityInspector(App, App->SelectedEntity);
            break;
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void ParametricSpectrumViewerWindow(application* App)
{
    static float Spectrum[512] = {};
    static glm::vec3 Color = {};

    ImGui::Begin("Parametric Spectrum Viewer");

    if (ImGui::ColorEdit3("Color", &Color[0], ImGuiColorEditFlags_Float)) {
        glm::vec3 Beta = GetParametricSpectrumCoefficients(App->Scene->RGBSpectrumTable, Color);
        for (int I = 0; I < IM_ARRAYSIZE(Spectrum); I++) {
            float Lambda = glm::mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, I / 512.f);
            Spectrum[I] = SampleParametricSpectrum(Beta, Lambda);
        }
    }

    ImVec2 Size = ImGui::GetWindowSize();
    Size.x -= 40.0f;
    Size.y -= 100.0f;

    ImGui::PlotLines("Spectrum", Spectrum, IM_ARRAYSIZE(Spectrum), 0, 0, 0.0f, 1.0f, Size);
    ImGui::End();
}

void InitializeUI(application* App)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImVec4* Colors = ImGui::GetStyle().Colors;
    Colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    Colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    Colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    Colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    Colors[ImGuiCol_PopupBg]                = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    Colors[ImGuiCol_Border]                 = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    Colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    Colors[ImGuiCol_FrameBg]                = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    Colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    Colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    Colors[ImGuiCol_TitleBg]                = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    Colors[ImGuiCol_TitleBgActive]          = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    Colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    Colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    Colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    Colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    Colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    Colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    Colors[ImGuiCol_CheckMark]              = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    Colors[ImGuiCol_SliderGrab]             = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    Colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    Colors[ImGuiCol_Button]                 = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    Colors[ImGuiCol_ButtonHovered]          = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    Colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    Colors[ImGuiCol_Header]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    Colors[ImGuiCol_HeaderHovered]          = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    Colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    Colors[ImGuiCol_Separator]              = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    Colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    Colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    Colors[ImGuiCol_ResizeGrip]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    Colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    Colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    Colors[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    Colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    Colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    Colors[ImGuiCol_TabUnfocused]           = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    Colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    Colors[ImGuiCol_DockingPreview]         = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    Colors[ImGuiCol_DockingEmptyBg]         = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    Colors[ImGuiCol_PlotLines]              = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    Colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    Colors[ImGuiCol_PlotHistogram]          = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    Colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    Colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    Colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    Colors[ImGuiCol_TableBorderLight]       = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    Colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    Colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    Colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    Colors[ImGuiCol_DragDropTarget]         = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    Colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    Colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    Colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    Colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    ImGuiStyle& Style = ImGui::GetStyle();
    Style.WindowPadding                     = ImVec2(8.00f, 8.00f);
    Style.FramePadding                      = ImVec2(10.00f, 4.00f);
    Style.CellPadding                       = ImVec2(6.00f, 6.00f);
    Style.ItemSpacing                       = ImVec2(3.00f, 3.00f);
    Style.ItemInnerSpacing                  = ImVec2(3.00f, 3.00f);
    Style.TouchExtraPadding                 = ImVec2(0.00f, 0.00f);
    Style.IndentSpacing                     = 25;
    Style.ScrollbarSize                     = 15;
    Style.GrabMinSize                       = 10;
    Style.WindowBorderSize                  = 1;
    Style.ChildBorderSize                   = 1;
    Style.PopupBorderSize                   = 1;
    Style.FrameBorderSize                   = 1;
    Style.TabBorderSize                     = 1;
    Style.WindowRounding                    = 7;
    Style.ChildRounding                     = 4;
    Style.FrameRounding                     = 3;
    Style.PopupRounding                     = 4;
    Style.ScrollbarRounding                 = 9;
    Style.GrabRounding                      = 3;
    Style.LogSliderDeadzone                 = 4;
    Style.TabRounding                       = 4;
}
