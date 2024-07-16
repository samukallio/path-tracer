#include <imgui.h>
#include <imgui_internal.h>

#define IMGUI_IMPLEMENTATION

static bool TextureSelectorDropDown(char const* Label, struct scene* Scene, struct texture** TexturePtr);

#include "core/common.hpp"
#include "core/vulkan.hpp"
#include "scene/scene.hpp"
#include "integrator/integrator.hpp"
#include "application/application.hpp"
#include "application/imgui_font.hpp"

#include <misc/cpp/imgui_stdlib.h>
#include <nfd.h>

static std::optional<std::filesystem::path> OpenDialog(std::span<nfdu8filteritem_t> Filters)
{
    auto CurrentPath = std::filesystem::current_path().string();
    nfdu8char_t* Path = nullptr;
    nfdresult_t Result = NFD_OpenDialogU8(&Path, Filters.data(), static_cast<nfdfiltersize_t>(Filters.size()), CurrentPath.c_str());
    std::optional<std::filesystem::path> OutPath = {};
    if (Result == NFD_OKAY) OutPath = Path; 
    if (Path) NFD_FreePathU8(Path);
    return OutPath;
};

static std::optional<std::filesystem::path> SaveDialog(std::span<nfdu8filteritem_t> Filters, char const* DefaultName)
{
    auto CurrentPath = std::filesystem::current_path().string();
    nfdu8char_t* Path = nullptr;
    nfdresult_t Result = NFD_SaveDialogU8(&Path, Filters.data(), static_cast<nfdfiltersize_t>(Filters.size()), CurrentPath.c_str(), DefaultName);
    std::optional<std::string> OutPath = {};
    if (Result == NFD_OKAY) OutPath = Path; 
    if (Path) NFD_FreePathU8(Path);
    return OutPath;
};

static bool DragEulerAngles(const char* Label, vec3* Angles)
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
static bool ResourceSelectorDropDown
(
    char const* Label,
    std::vector<resource_t*>& Resources,
    resource_t** ResourcePtr
)
{
    auto GetItemName = [](void* Context, int Index)
    {
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

    if (Changed)
    {
        if (Index == 0)
            *ResourcePtr = nullptr;
        else
            *ResourcePtr = Resources[Index-1];
    }

    return Changed;
}

static bool TextureSelectorDropDown(char const* Label, scene* Scene, texture** TexturePtr)
{
    return ResourceSelectorDropDown(Label, Scene->Textures, TexturePtr);
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

    if (ImGui::BeginCombo("Type", TextureTypeName(Texture->Type)))
    {
        for (int I = 0; I < TEXTURE_TYPE__COUNT; I++)
        {
            auto Type = static_cast<texture_type>(I);
            bool IsSelected = Texture->Type == Type;
            if (ImGui::Selectable(TextureTypeName(Type), &IsSelected))
            {
                Texture->Type = Type;
                C = true;
            }
        }
        ImGui::EndCombo();
    }

    C |= ImGui::Checkbox("Nearest Filtering", &Texture->EnableNearestFiltering);

    size_t TextureID = Texture->PackedTextureIndex + 1;
    float Width = ImGui::GetWindowWidth() - 16;
    ImGui::Image(reinterpret_cast<ImTextureID>(TextureID), ImVec2(Width, Width));

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_TEXTURES;

    ImGui::PopID();
}

static void MaterialInspector(application* App, material* Material, bool Referenced = false)
{
    scene* Scene = App->Scene;

    if (!Material) return;

    ImGui::PushID(Material);

    if (Referenced)
    {
        char Title[256];
        snprintf(Title, 256, "Material: %s", Material->Name.c_str());
        ImGui::SeparatorText(Title);
    }
    else
    {
        ImGui::SeparatorText("Material");
        ImGui::InputText("Name", &Material->Name);
    }

    bool C = false;

    if (ImGui::BeginCombo("Material Type", MaterialTypeName(Material->Type)))
    {
        for (int I = 0; I < MATERIAL_TYPE__COUNT; I++)
        {
            auto MaterialType = static_cast<material_type>(I);
            bool IsSelected = Material->Type == MaterialType;
            if (ImGui::Selectable(MaterialTypeName(MaterialType), &IsSelected))
            {
                material* NewMaterial = CreateMaterial(Scene, MaterialType, Material->Name.c_str());

                if (App->SelectedMaterial == Material)
                    App->SelectedMaterial = NewMaterial;

                ReplaceMaterialReferences(Scene, Material, NewMaterial);
                DestroyMaterial(Scene, Material);
                C = true;
            }
        }
        ImGui::EndCombo();
    }

    C |= MaterialInspectorX(App->Scene, Material);

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_MATERIALS;

    ImGui::PopID();
}

static void MeshInspector(application* App, mesh* Mesh, bool Referenced = false)
{
    scene* Scene = App->Scene;

    if (!Mesh) return;

    ImGui::PushID(Mesh);

    if (Referenced)
    {
        char Title[256];
        snprintf(Title, 256, "Mesh: %s", Mesh->Name.c_str());
        ImGui::SeparatorText(Title);
    }
    else
    {
        ImGui::SeparatorText("Mesh");
        ImGui::InputText("Name", &Mesh->Name);
    }

    bool C = false;

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_MESHES;

    ImGui::PopID();
}

static void CameraInspector(application* App, camera_entity* Camera)
{
    bool C = false;

    ImGui::Spacing();
    ImGui::SeparatorText("Projection");

    if (ImGui::BeginCombo("Camera Model", CameraModelName(Camera->CameraModel)))
    {
        for (int I = 0; I < CAMERA_MODEL__COUNT; I++)
        {
            auto CameraModel = static_cast<camera_model>(I);
            bool IsSelected = Camera->CameraModel == CameraModel;
            if (ImGui::Selectable(CameraModelName(CameraModel), &IsSelected))
            {
                Camera->CameraModel = CameraModel;
                C = true;
            }
        }
        ImGui::EndCombo();
    }

    if (Camera->CameraModel == CAMERA_MODEL_PINHOLE)
    {
        C |= ImGui::DragFloat("FOV (degrees)", &Camera->Pinhole.FieldOfViewInDegrees, 1.0f, 0.01f, 179.99f);
        C |= ImGui::DragFloat("Aperture (mm)", &Camera->Pinhole.ApertureDiameterInMM, 0.1f, 0.0f, 50.0f);
    }

    if (Camera->CameraModel == CAMERA_MODEL_THIN_LENS)
    {
        vec2 SensorSizeInMM = Camera->ThinLens.SensorSizeInMM;
        if (ImGui::DragFloat2("Sensor Size (mm)", &SensorSizeInMM[0], 1.0f, 1.0f, 100.0f))
        {
            float const ASPECT_RATIO = 2048.0f / 1024.0f;
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

    ImGui::Spacing();
    ImGui::SeparatorText("Rendering");

    if (App->SceneCameraToRender == Camera)
    {
        bool Active = true;
        C |= ImGui::Checkbox("Render Using This Camera", &Active);
        if (!Active) App->SceneCameraToRender = nullptr;
    }
    else
    {
        bool Active = false;
        C |= ImGui::Checkbox("Render Using This Camera", &Active);
        if (Active) App->SceneCameraToRender = Camera;
    }

    //if (ImGui::BeginCombo("Render Mode", RenderModeName(Camera->RenderMode))) {
    //    for (int I = 0; I < RENDER_MODE__COUNT; I++) {
    //        auto RenderMode = static_cast<render_mode>(I);
    //        bool IsSelected = Camera->RenderMode == RenderMode;
    //        if (ImGui::Selectable(RenderModeName(RenderMode), &IsSelected)) {
    //            Camera->RenderMode = RenderMode;
    //            C = true;
    //        }
    //    }
    //    ImGui::EndCombo();
    //}
    //
    //if (Camera->RenderMode == RENDER_MODE_PATH_TRACE) {
    //    int BounceLimit = static_cast<int>(Camera->RenderBounceLimit);
    //    C |= ImGui::InputInt("Bounce Limit", &BounceLimit);
    //    C |= ImGui::DragFloat("Termination Probability", &Camera->RenderTerminationProbability, 0.001f, 0.0f, 1.0f);
    //    Camera->RenderBounceLimit = std::max(1, BounceLimit);
    //}

    //char const* const RenderSampleBlockSizeLabels[] = { "1x1", "2x2", "4x4", "8x8" };
    //ImGui::Combo("Sample Block Size",
    //    (int*)&Camera->RenderSampleBlockSizeLog2,
    //    RenderSampleBlockSizeLabels, 4);

    ImGui::Spacing();

    if (C) App->Scene->DirtyFlags |= SCENE_DIRTY_CAMERAS;
}

static void EntityInspector(application* App, entity* Entity)
{
    scene* Scene = App->Scene;

    if (!Entity) return;

    ImGui::PushID(Entity);
    ImGui::SeparatorText(EntityTypeName(Entity->Type));

    bool C = false;

    if (Entity->Type != ENTITY_TYPE_ROOT)
    {
        C |= ImGui::Checkbox("Active", &Entity->Active);

        ImGui::InputText("Name", &Entity->Name);

        transform& Transform = Entity->Transform;
        C |= ImGui::DragFloat3("Position", &Transform.Position[0], 0.1f);
        C |= DragEulerAngles("Rotation", &Transform.Rotation);

        if (Entity->Type != ENTITY_TYPE_CAMERA)
        {
            vec3 Scale = Transform.Scale;
            if (ImGui::DragFloat3("Scale", &Scale[0], 0.01f))
            {
                if (Transform.ScaleIsUniform)
                {
                    for (int I = 0; I < 3; I++)
                    {
                        if (Scale[I] == Transform.Scale[I])
                            continue;
                        Scale = vec3(1) * Scale[I];
                        break;
                    }
                }
                C = true;
            }
            if (ImGui::Checkbox("Uniform Scale", &Transform.ScaleIsUniform))
            {
                if (Transform.ScaleIsUniform)
                    Scale = vec3(1) * Scale.x;
                C = true;
            }
            Transform.Scale = Scale;
        }
    }

    switch (Entity->Type)
    {
        case ENTITY_TYPE_ROOT:
        {
            auto Root = static_cast<root_entity*>(Entity);
            C |= ImGui::DragFloat("Scattering Rate", &Root->ScatterRate, 0.001f, 0.00001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            C |= ImGui::DragFloat("Skybox Brightness", &Root->SkyboxBrightness, 0.01f, 0.0f, 1.0f);
            C |= ImGui::DragFloat("Skybox Sampling Probability", &Root->SkyboxSamplingProbability, 0.01f, 0.0f, 1.0f);

            if (ResourceSelectorDropDown("Skybox Texture", Scene->Textures, &Root->SkyboxTexture))
                Scene->DirtyFlags |= SCENE_DIRTY_SKYBOX_TEXTURE;

            if (C) Scene->DirtyFlags |= SCENE_DIRTY_GLOBALS;
            break;
        }
        case ENTITY_TYPE_CAMERA:
        {
            CameraInspector(App, static_cast<camera_entity*>(Entity));
            if (C) Scene->DirtyFlags |= SCENE_DIRTY_CAMERAS;
            break;
        }
        case ENTITY_TYPE_MESH_INSTANCE:
        {
            auto Instance = static_cast<mesh_entity*>(Entity);
            C |= ResourceSelectorDropDown("Mesh", Scene->Meshes, &Instance->Mesh);
            C |= ResourceSelectorDropDown("Material", Scene->Materials, &Instance->Material);
            ImGui::Spacing();
            MeshInspector(App, Instance->Mesh, true);
            MaterialInspector(App, Instance->Material, true);
            break;
        }
        case ENTITY_TYPE_PLANE:
        {
            auto Plane = static_cast<plane_entity*>(Entity);
            C |= ResourceSelectorDropDown("Material", Scene->Materials, &Plane->Material);
            ImGui::Spacing();
            MaterialInspector(App, Plane->Material, true);
            break;
        }
        case ENTITY_TYPE_SPHERE:
        {
            auto Sphere = static_cast<sphere_entity*>(Entity);
            C |= ResourceSelectorDropDown("Material", Scene->Materials, &Sphere->Material);
            ImGui::Spacing();
            MaterialInspector(App, Sphere->Material, true);
            break;
        }
        case ENTITY_TYPE_CUBE:
        {
            auto Cube = static_cast<cube_entity*>(Entity);
            C |= ResourceSelectorDropDown("Material", Scene->Materials, &Cube->Material);
            ImGui::Spacing();
            MaterialInspector(App, Cube->Material, true);
            break;
        }
    }

    if (C) Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;

    ImGui::PopID();
}

static void EntityTreeNode(application* App, entity* Entity, bool PrefabMode=false)
{
    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (Entity->Children.empty())
        Flags |= ImGuiTreeNodeFlags_Leaf;

    if (Entity->Type == ENTITY_TYPE_ROOT)
        Flags |= ImGuiTreeNodeFlags_DefaultOpen;

    if (App->SelectionType == SELECTION_TYPE_ENTITY && App->SelectedEntity == Entity)
        Flags |= ImGuiTreeNodeFlags_Selected;

    if (!Entity->Active)
    {
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.x *= 0.5f;
        color.y *= 0.5f;
        color.z *= 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
    }

    bool IsOpen = ImGui::TreeNodeEx(Entity->Name.c_str(), Flags);
    bool IsDestroyed = false;

    if (!PrefabMode)
    {
        if (ImGui::BeginPopupContextItem())
        {
            for (int I = 0; I < ENTITY_TYPE__COUNT; I++)
            {
                if (I == ENTITY_TYPE_ROOT) continue;
                char Buffer[256];
                auto EntityType = static_cast<entity_type>(I);
                snprintf(Buffer, std::size(Buffer), "Create %s...", EntityTypeName(EntityType));
                if (ImGui::MenuItem(Buffer))
                {
                    auto Child = CreateEntity(App->Scene, EntityType, Entity);
                    Child->Name = std::format("New {}", EntityTypeName(EntityType));
                    App->Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
                    App->SelectionType = SELECTION_TYPE_ENTITY;
                    App->SelectedEntity = Child;
                }
            }
            if (!App->Scene->Prefabs.empty())
            {
                if (ImGui::BeginMenu("Create Prefab Instance"))
                {
                    for (prefab* Prefab : App->Scene->Prefabs)
                    {
                        if (ImGui::MenuItem(Prefab->Entity->Name.c_str()))
                        {
                            auto Child = CreateEntity(App->Scene, Prefab, Entity);
                            App->Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
                            App->SelectionType = SELECTION_TYPE_ENTITY;
                            App->SelectedEntity = Child;
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            if (Entity->Type != ENTITY_TYPE_ROOT)
            {
                if (ImGui::MenuItem("Delete"))
                    IsDestroyed = true;
            }
            ImGui::EndPopup();
        }

        if (ImGui::IsItemClicked())
        {
            App->SelectionType = SELECTION_TYPE_ENTITY;
            App->SelectedEntity = Entity;
        }
    }

    // Draw the entity type on the right hand side.
    {
        char const* TypeText = EntityTypeName(Entity->Type);
        ImVec2 TypeLabelSize = ImGui::CalcTextSize(TypeText);
        ImGui::SameLine(ImGui::GetWindowWidth() - TypeLabelSize.x - 10);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::Text(TypeText);
        ImGui::PopStyleColor();
    }

    if (IsOpen)
    {
        for (entity* Child : Entity->Children)
            EntityTreeNode(App, Child, PrefabMode);

        ImGui::TreePop();
    }

    if (!Entity->Active)
    {
        ImGui::PopStyleColor();
    }

    if (IsDestroyed)
    {
        if (App->SelectedEntity == Entity)
        {
            App->SelectionType = SELECTION_TYPE_NONE;
            App->SelectedEntity = nullptr;
        }
        if (App->SceneCameraToRender == Entity)
        {
            App->SceneCameraToRender = nullptr;
        }
        DestroyEntity(App->Scene, Entity);
        App->Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
    }
}

static void PrefabInspector(application* App, prefab* Prefab, bool Referenced = false)
{
    ImGui::PushID(Prefab);

    if (Referenced)
    {
        char Title[256];
        snprintf(Title, 256, "Prefab: %s", Prefab->Entity->Name.c_str());
        ImGui::SeparatorText(Title);
    }
    else
    {
        ImGui::SeparatorText("Prefab");
        ImGui::InputText("Name", &Prefab->Entity->Name);
    }

    EntityTreeNode(App, Prefab->Entity, true);

    ImGui::PopID();
}

void TextureBrowserWindow(application* App)
{
    ImGui::Begin("Textures");

    scene* Scene = App->Scene;

    if (ImGui::Button("Import..."))
    {
        nfdu8filteritem_t Filters[] =
        {
            { "Portable Network Graphics", "png" },
            { "High-Dynamic Range Image", "hdr" },
        };
        std::optional<std::filesystem::path> Path = OpenDialog(Filters);
        if (Path.has_value())
        {
            load_model_options Options;
            Options.DirectoryPath = Path.value().parent_path().string();
            App->SelectedTexture = LoadTexture(App->Scene, Path.value().string().c_str(), TEXTURE_TYPE_RAW);
            App->SelectionType = SELECTION_TYPE_TEXTURE;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(App->SelectionType != SELECTION_TYPE_TEXTURE);
    if (ImGui::Button("Delete"))
    {
        DestroyTexture(App->Scene, App->SelectedTexture);
        App->SelectedTexture = nullptr;
        App->SelectionType = SELECTION_TYPE_NONE;
    }
    ImGui::EndDisabled();

    auto GetItemName = [](void* Context, int Index)
    {
        auto Scene = static_cast<scene*>(Context);
        if (Index < 0 || Index >= Scene->Textures.size())
            return "";
        return Scene->Textures[Index]->Name.c_str();
    };

    int Index = -1;
    int Count = static_cast<int>(Scene->Textures.size());

    if (App->SelectionType == SELECTION_TYPE_TEXTURE)
    {
        for (int I = 0; I < Count; I++)
            if (Scene->Textures[I] == App->SelectedTexture)
                Index = I;
    }

    if (ImGui::ListBox("Textures", &Index, GetItemName, Scene, Count, 6))
    {
        App->SelectionType = SELECTION_TYPE_TEXTURE;
        App->SelectedTexture = Scene->Textures[Index];
    }

    ImGui::End();
}

void MaterialBrowserWindow(application* App)
{
    ImGui::Begin("Materials");

    scene* Scene = App->Scene;

    if (ImGui::Button("New"))
    {
        App->SelectedMaterial = CreateMaterial(App->Scene, MATERIAL_TYPE_OPENPBR, "New Material");
        App->SelectionType = SELECTION_TYPE_MATERIAL;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(App->SelectionType != SELECTION_TYPE_MATERIAL);
    if (ImGui::Button("Clone"))
    {
        material* Clone = CreateMaterial(App->Scene, MATERIAL_TYPE_OPENPBR, "");
        *Clone = *App->SelectedMaterial;
        Clone->Name = std::format("{} (Clone)", App->SelectedMaterial->Name);
        App->SelectedMaterial = Clone;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
    {
        DestroyMaterial(App->Scene, App->SelectedMaterial);
        App->SelectedMaterial = nullptr;
        App->SelectionType = SELECTION_TYPE_NONE;
    }
    ImGui::EndDisabled();


    auto GetItemName = [](void* Context, int Index)
    {
        auto Scene = static_cast<scene*>(Context);
        if (Index < 0 || Index >= Scene->Materials.size())
            return "";
        return Scene->Materials[Index]->Name.c_str();
    };

    int Index = -1;
    int Count = static_cast<int>(Scene->Materials.size());

    if (App->SelectionType == SELECTION_TYPE_MATERIAL)
    {
        for (int I = 0; I < Count; I++)
            if (Scene->Materials[I] == App->SelectedMaterial)
                Index = I;
    }

    if (ImGui::ListBox("Materials", &Index, GetItemName, Scene, Count, 6))
    {
        App->SelectionType = SELECTION_TYPE_MATERIAL;
        App->SelectedMaterial = Scene->Materials[Index];
    }

    ImGui::End();
}

void MeshBrowserWindow(application* App)
{
    ImGui::Begin("Meshes");

    ImGui::BeginDisabled(App->SelectionType != SELECTION_TYPE_MESH);
    if (ImGui::Button("Delete"))
    {
        DestroyMesh(App->Scene, App->SelectedMesh);
        App->SelectedMesh = nullptr;
        App->SelectionType = SELECTION_TYPE_NONE;
    }
    ImGui::EndDisabled();

    scene* Scene = App->Scene;

    auto GetItemName = [](void* Context, int Index)
    {
        auto Scene = static_cast<scene*>(Context);
        if (Index < 0 || Index >= Scene->Meshes.size())
            return "";
        return Scene->Meshes[Index]->Name.c_str();
    };

    int Index = -1;
    int Count = static_cast<int>(Scene->Meshes.size());

    if (App->SelectionType == SELECTION_TYPE_MESH)
    {
        for (int I = 0; I < Count; I++)
            if (Scene->Meshes[I] == App->SelectedMesh)
                Index = I;
    }

    if (ImGui::ListBox("Meshes", &Index, GetItemName, Scene, Count, 6))
    {
        App->SelectionType = SELECTION_TYPE_MESH;
        App->SelectedMesh = Scene->Meshes[Index];
    }

    ImGui::End();
}

void PrefabBrowserWindow(application* App)
{
    ImGui::Begin("Prefabs");

    if (ImGui::Button("Import Model..."))
    {
        nfdu8filteritem_t Filters[] =
        {
            { "Wavefront OBJ", "obj" }
        };
        std::optional<std::filesystem::path> Path = OpenDialog(Filters);
        if (Path.has_value()) {
            load_model_options Options;
            Options.DirectoryPath = Path.value().parent_path().string();
            App->SelectedPrefab = LoadModelAsPrefab(App->Scene, Path.value().string().c_str(), &Options);
            App->SelectionType = SELECTION_TYPE_PREFAB;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(App->SelectionType != SELECTION_TYPE_PREFAB);
    if (ImGui::Button("Delete"))
    {
        DestroyPrefab(App->Scene, App->SelectedPrefab);
        App->SelectedPrefab = nullptr;
        App->SelectionType = SELECTION_TYPE_NONE;
    }
    ImGui::EndDisabled();

    scene* Scene = App->Scene;

    auto GetItemName = [](void* Context, int Index)
    {
        auto Scene = static_cast<scene*>(Context);
        if (Index < 0 || Index >= Scene->Prefabs.size())
            return "";
        return Scene->Prefabs[Index]->Entity->Name.c_str();
    };

    int Index = -1;
    int Count = static_cast<int>(Scene->Prefabs.size());

    if (App->SelectionType == SELECTION_TYPE_PREFAB)
    {
        for (int I = 0; I < Count; I++)
            if (Scene->Prefabs[I] == App->SelectedPrefab)
                Index = I;
    }

    if (ImGui::ListBox("Prefabs", &Index, GetItemName, Scene, Count, 6))
    {
        App->SelectionType = SELECTION_TYPE_PREFAB;
        App->SelectedPrefab = Scene->Prefabs[Index];
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

    switch (App->SelectionType)
    {
        case SELECTION_TYPE_TEXTURE:
            TextureInspector(App, App->SelectedTexture);
            break;
        case SELECTION_TYPE_MATERIAL:
            MaterialInspector(App, App->SelectedMaterial);
            break;
        case SELECTION_TYPE_MESH:
            MeshInspector(App, App->SelectedMesh);
            break;
        case SELECTION_TYPE_PREFAB:
            PrefabInspector(App, App->SelectedPrefab);
            break;
        case SELECTION_TYPE_ENTITY:
            EntityInspector(App, App->SelectedEntity);
            break;
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void PreviewSettingsWindow(application* App, ImGuiDockNode* Node)
{
    if (App->SceneCameraToRender) return;

    ImVec2 Size = { 400, 70 };
    ImVec2 Margin = { 16, 16 };

    ImVec2 Position = Node->Pos;
    Position.x = Node->Pos.x + Node->Size.x - Size.x - Margin.x;
    Position.y = Node->Pos.y + Margin.y;
    ImGui::SetNextWindowPos(Position);
    ImGui::SetNextWindowSize(Size);
    ImGui::SetNextWindowBgAlpha(0.5f);

    bool Open = true;
    ImGui::Begin("Preview Settings", &Open,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking);

    ImGui::PushItemWidth(0.50f * ImGui::GetWindowWidth());

    if (ImGui::BeginCombo("Preview Mode", PreviewRenderModeName(App->PreviewRenderMode)))
    {
        for (int I = 0; I < PREVIEW_RENDER_MODE__COUNT; I++)
        {
            auto RenderMode = static_cast<preview_render_mode>(I);
            bool IsSelected = App->PreviewRenderMode == RenderMode;
            if (ImGui::Selectable(PreviewRenderModeName(RenderMode), &IsSelected))
                App->PreviewRenderMode = RenderMode;
        }
        ImGui::EndCombo();
    }

    ImGui::SliderFloat("Brightness", &App->PreviewBrightness, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

    ImGui::End();
}

void RenderSettingsWindow(application* App, ImGuiDockNode* Node)
{
    if (!App->SceneCameraToRender) return;

    ImVec2 Size = { 400, 120 };
    ImVec2 Margin = { 16, 16 };

    ImVec2 Position = Node->Pos;
    Position.x = Node->Pos.x + Node->Size.x - Size.x - Margin.x;
    Position.y = Node->Pos.y + Margin.y;
    ImGui::SetNextWindowPos(Position);
    ImGui::SetNextWindowSize(Size);
    ImGui::SetNextWindowBgAlpha(0.5f);

    bool Open = true;
    ImGui::Begin("Render Settings", &Open,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking);

    ImGui::Spacing();
    ImGui::SeparatorText("Post-Processing");

    ImGui::PushItemWidth(0.50f * ImGui::GetWindowWidth());

    auto ResolveParameters = &App->ResolveParameters;

    ImGui::SliderFloat("Brightness", &ResolveParameters->Brightness, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

    if (ImGui::BeginCombo("Tone Mapping", ToneMappingModeName(ResolveParameters->ToneMappingMode)))
    {
        for (int I = 0; I < TONE_MAPPING_MODE__COUNT; I++)
        {
            auto ToneMappingMode = static_cast<tone_mapping_mode>(I);
            bool IsSelected = ResolveParameters->ToneMappingMode == ToneMappingMode;
            if (ImGui::Selectable(ToneMappingModeName(ToneMappingMode), &IsSelected))
            {
                ResolveParameters->ToneMappingMode = ToneMappingMode;
            }
        }
        ImGui::EndCombo();
    }

    if (ResolveParameters->ToneMappingMode == TONE_MAPPING_MODE_REINHARD)
    {
        ImGui::SliderFloat("White Level", &ResolveParameters->ToneMappingWhiteLevel, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    //ImGui::CheckboxFlags("Sample Accumulation", &Camera->RenderFlags, RENDER_FLAG_ACCUMULATE);
    //ImGui::CheckboxFlags("Sample Jitter", &Camera->RenderFlags, RENDER_FLAG_SAMPLE_JITTER);

    ImGui::End();
}

void ParametricSpectrumViewerWindow(application* App)
{
    static float Spectrum[512] = {};
    static vec3 Color = {};

    ImGui::Begin("Parametric Spectrum Viewer");

    if (ImGui::ColorEdit3("Color", &Color[0], ImGuiColorEditFlags_Float))
    {
        vec3 Beta = GetParametricSpectrumCoefficients(App->Scene->RGBSpectrumTable, Color);
        for (int I = 0; I < IM_ARRAYSIZE(Spectrum); I++)
        {
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

void MainMenuBar(application* App)
{
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene"))
        {
            DestroyScene(App->Scene);
            App->SelectionType = SELECTION_TYPE_NONE;
            App->SceneCameraToRender = nullptr;
            App->Scene = CreateScene();
            App->Scene->DirtyFlags = SCENE_DIRTY_ALL;
        }
        if (ImGui::MenuItem("Open Scene..."))
        {
            nfdu8filteritem_t Filters[] = { { "Scene File", "json" } };
            std::optional<std::filesystem::path> Path = OpenDialog(Filters);
            if (Path.has_value())
            {
                scene* Scene = LoadScene(Path.value().string().c_str());
                if (Scene)
                {
                    DestroyScene(App->Scene);
                    App->SelectionType = SELECTION_TYPE_NONE;
                    App->SceneCameraToRender = nullptr;
                    App->Scene = Scene;
                }
            }
        }
        if (ImGui::MenuItem("Save Scene As..."))
        {
            nfdu8filteritem_t Filters[] = { { "Scene File", "json" } };
            std::optional<std::filesystem::path> Path = SaveDialog(Filters, "scene.json");
            if (Path.has_value())
            {
                SaveScene(Path.value().string().c_str(), App->Scene);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void CreateImGui(application* App)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    IO.Fonts->AddFontFromMemoryCompressedTTF(CousineRegular_compressed_data, CousineRegular_compressed_size, 16);
    IO.Fonts->Build();

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

void DestroyImGui(application* App)
{
    ImGui::DestroyContext();
}

void ShowImGui(application* App)
{
    MainMenuBar(App);

    ImGuiID NodeID = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGuiDockNode* Node = ImGui::DockBuilderGetCentralNode(NodeID);
    PreviewSettingsWindow(App, Node);
    RenderSettingsWindow(App, Node);

    ImGui::ShowDemoWindow();
    InspectorWindow(App);
    TextureBrowserWindow(App);
    MaterialBrowserWindow(App);
    MeshBrowserWindow(App);
    PrefabBrowserWindow(App);
    SceneHierarchyWindow(App);
    ParametricSpectrumViewerWindow(App);
}
