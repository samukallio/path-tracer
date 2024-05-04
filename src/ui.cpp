#include "common.h"
#include "ui.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

static bool DragEulerAngles(const char* label, glm::vec3* angles)
{
    float degrees[3];
    for (int k = 0; k < 3; k++)
        degrees[k] = RepeatRange((*angles)[k], -PI, +PI) * (360 / TAU);
    bool changed = ImGui::DragFloat3(label, degrees);
    for (int k = 0; k < 3; k++)
        (*angles)[k] = RepeatRange(degrees[k] * (TAU / 360), -PI, +PI);
    return changed;
}

template<typename ResourceT>
static bool ResourceSelectorDropDown(
    char const* label,
    std::vector<ResourceT*>& resources,
    ResourceT** resourcePtr)
{
    auto getter = [](void* context, int index) {
        auto& resources = *static_cast<std::vector<ResourceT*>*>(context);
        if (index <= 0)
            return "(none)";
        if (index > resources.size())
            return "";
        return resources[index-1]->name.c_str();
    };

    int itemIndex = 0;
    int itemCount = static_cast<int>(resources.size()) + 1;

    for (int k = 0; k < itemCount-1; k++)
        if (resources[k] == *resourcePtr)
            itemIndex = k + 1;

    bool changed = ImGui::Combo(label, &itemIndex, getter, &resources, itemCount, 6);

    if (changed) {
        if (itemIndex == 0)
            *resourcePtr = nullptr;
        else
            *resourcePtr = resources[itemIndex-1];
    }

    return changed;
}

static void TextureInspector(UIContext* context, Texture* texture)
{
    Scene& scene = *context->scene;

    if (!texture) return;

    ImGui::PushID(texture);

    ImGui::SeparatorText("Texture");

    ImGui::InputText("Name", &texture->name);
    ImGui::LabelText("Size", "%u x %u", texture->width, texture->height);

    ImGui::PopID();
}

static void MaterialInspector(UIContext* context, Material* material, bool referenced = false)
{
    Scene& scene = *context->scene;

    if (!material) return;

    ImGui::PushID(material);

    if (referenced) {
        char title[256];
        snprintf(title, 256, "Material: %s", material->name.c_str());
        ImGui::SeparatorText(title);
    }
    else {
        ImGui::SeparatorText("Material");
        ImGui::InputText("Name", &material->name);
    }

    bool c = false;
    c |= ImGui::ColorEdit3("Base Color", &material->baseColor[0]);
    c |= ResourceSelectorDropDown("Base Color Texture", scene.textures, &material->baseColorTexture);
    c |= ImGui::Checkbox("Base Color Texture Filter Nearest", &material->baseColorTextureFilterNearest);
    c |= ImGui::ColorEdit3("Emission Color", &material->emissionColor[0]);
    c |= ImGui::DragFloat("Emission Power", &material->emissionPower, 1.0f, 0.0f, 100.0f);
    c |= ResourceSelectorDropDown("Emission Color Texture", scene.textures, &material->emissionColorTexture);
    c |= ImGui::DragFloat("Metallic", &material->metallic, 0.01f, 0.0f, 1.0f);
    c |= ResourceSelectorDropDown("Metallic Texture", scene.textures, &material->metallicTexture);
    c |= ImGui::DragFloat("Roughness", &material->roughness, 0.01f, 0.0f, 1.0f);
    c |= ResourceSelectorDropDown("Roughness Texture", scene.textures, &material->roughnessTexture);
    c |= ImGui::DragFloat("Refraction Probability", &material->refraction, 0.01f, 0.0f, 1.0f);
    c |= ImGui::DragFloat("Refraction Index", &material->refractionIndex, 0.001f, 2.0f);
    if (c) scene.dirtyFlags |= SCENE_DIRTY_MATERIALS;

    ImGui::PopID();
}

static void MeshInspector(UIContext* context, Mesh* mesh, bool referenced = false)
{
    Scene& scene = *context->scene;

    if (!mesh) return;

    ImGui::PushID(mesh);

    if (referenced) {
        char title[256];
        snprintf(title, 256, "Mesh: %s", mesh->name.c_str());
        ImGui::SeparatorText(title);
    }
    else {
        ImGui::SeparatorText("Mesh");
        ImGui::InputText("Name", &mesh->name);
    }

    bool c = false;

    ImGui::PushID("materials");
    for (size_t k = 0; k < mesh->materials.size(); k++) {
        ImGui::PushID(static_cast<int>(k));

        char title[32];
        sprintf_s(title, "Material %llu", k);
        c |= ResourceSelectorDropDown(title, scene.materials, &mesh->materials[k]);

        ImGui::PopID();
    }
    ImGui::PopID();

    for (size_t k = 0; k < mesh->materials.size(); k++) {
        ImGui::Spacing();
        MaterialInspector(context, mesh->materials[k], true);
    }

    if (c) scene.dirtyFlags |= SCENE_DIRTY_MESHES;

    ImGui::PopID();
}

static void CameraInspector(UIContext* context, Camera* camera)
{
    bool c = false;

    if (context->camera == camera) {
        bool active = true;
        ImGui::Checkbox("Possess", &active);
        if (!active) context->camera = nullptr;
    }
    else {
        bool active = false;
        ImGui::Checkbox("Possess", &active);
        if (active) context->camera = camera;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Rendering");

    if (ImGui::BeginCombo("Render Mode", RenderModeName(camera->renderMode))) {
        for (int k = 0; k < RENDER_MODE__COUNT; k++) {
            auto renderMode = static_cast<RenderMode>(k);
            bool selected = camera->renderMode == renderMode;
            if (ImGui::Selectable(RenderModeName(renderMode), &selected)) {
                camera->renderMode = renderMode;
                c = true;
            }
        }
        ImGui::EndCombo();
    }
    
    if (camera->renderMode == RENDER_MODE_PATH_TRACE) {
        int bounceLimit = static_cast<int>(camera->renderBounceLimit);
        c |= ImGui::InputInt("Bounce Limit", &bounceLimit);
        camera->renderBounceLimit = std::max(1, bounceLimit);
    }

    if (camera->renderMode == RENDER_MODE_MESH_COMPLEXITY) {
        int complexityScale = static_cast<int>(camera->renderMeshComplexityScale);
        c |= ImGui::InputInt("Maximum Complexity", &complexityScale);
        camera->renderMeshComplexityScale = std::max(1, complexityScale);
    }

    if (camera->renderMode == RENDER_MODE_SCENE_COMPLEXITY) {
        int complexityScale = static_cast<int>(camera->renderSceneComplexityScale);
        c |= ImGui::InputInt("Maximum Complexity", &complexityScale);
        camera->renderSceneComplexityScale = std::max(1, complexityScale);
    }

    char const* const renderSampleBlockSizeLabels[] = { "1x1", "2x2", "4x4", "8x8" };
    ImGui::Combo("Sample Block Size",
        (int*)&camera->renderSampleBlockSizeLog2,
        renderSampleBlockSizeLabels, 4);

    c |= ImGui::CheckboxFlags("Sample Accumulation", &camera->renderFlags, RENDER_FLAG_ACCUMULATE);
    c |= ImGui::CheckboxFlags("Sample Jitter", &camera->renderFlags, RENDER_FLAG_SAMPLE_JITTER);

    ImGui::Spacing();
    ImGui::SeparatorText("Post-Processing");

    ImGui::SliderFloat("Brightness", &camera->brightness, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

    if (ImGui::BeginCombo("Tone Mapping", ToneMappingModeName(camera->toneMappingMode))) {
        for (int k = 0; k < TONE_MAPPING_MODE__COUNT; k++) {
            auto toneMappingMode = static_cast<ToneMappingMode>(k);
            bool selected = camera->toneMappingMode == toneMappingMode;
            if (ImGui::Selectable(ToneMappingModeName(toneMappingMode), &selected)) {
                camera->toneMappingMode = toneMappingMode;
            }
        }
        ImGui::EndCombo();
    }

    if (camera->toneMappingMode == TONE_MAPPING_MODE_REINHARD) {
        ImGui::SliderFloat("White Level", &camera->toneMappingWhiteLevel, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Projection");

    if (ImGui::BeginCombo("Camera Model", CameraModelName(camera->cameraModel))) {
        for (int k = 0; k < CAMERA_MODEL__COUNT; k++) {
            auto model = static_cast<CameraModel>(k);
            bool selected = camera->cameraModel == model;
            if (ImGui::Selectable(CameraModelName(model), &selected)) {
                camera->cameraModel = model;
                c = true;
            }
        }
        ImGui::EndCombo();
    }

    if (camera->cameraModel == CAMERA_MODEL_PINHOLE) {
        c |= ImGui::DragFloat("FOV (degrees)", &camera->pinhole.fieldOfViewInDegrees, 1.0f, 0.01f, 179.99f);
        c |= ImGui::DragFloat("Aperture (mm)", &camera->pinhole.apertureDiameterInMM, 0.1f, 0.0f, 50.0f);
    }

    if (camera->cameraModel == CAMERA_MODEL_THIN_LENS) {
        glm::vec2 sensorSizeInMM = camera->thinLens.sensorSizeInMM;
        if (ImGui::DragFloat2("Sensor Size (mm)", &sensorSizeInMM[0], 1.0f, 1.0f, 100.0f)) {
            float const ASPECT_RATIO = 1920.0f / 1080.0f;
            if (sensorSizeInMM.x != camera->thinLens.sensorSizeInMM.x)
                sensorSizeInMM.y = sensorSizeInMM.x / ASPECT_RATIO;
            else
                sensorSizeInMM.x = sensorSizeInMM.y * ASPECT_RATIO;
            camera->thinLens.sensorSizeInMM = sensorSizeInMM;
            c = true;
        }

        c |= ImGui::DragFloat("Focal Length (mm)", &camera->thinLens.focalLengthInMM, 1.0f, 1.0f, 200.0f);
        c |= ImGui::DragFloat("Aperture (mm)", &camera->thinLens.apertureDiameterInMM, 0.5f, 0.0f, 100.0f);
        c |= ImGui::DragFloat("Focus Distance", &camera->thinLens.focusDistance, 1.0f, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    if (c) context->scene->dirtyFlags |= SCENE_DIRTY_CAMERAS;
}

static void EntityInspector(UIContext* context, Entity* entity)
{
    Scene& scene = *context->scene;

    if (!entity) return;

    ImGui::PushID(entity);
    ImGui::SeparatorText(EntityTypeName(entity->type));

    bool c = false;

    if (entity->type != ENTITY_TYPE_ROOT) {
        c |= ImGui::Checkbox("Active", &entity->active);

        ImGui::InputText("Name", &entity->name);

        Transform& transform = entity->transform;
        c |= ImGui::DragFloat3("Position", &transform.position[0], 0.1f);
        c |= DragEulerAngles("Rotation", &transform.rotation);

        if (entity->type != ENTITY_TYPE_CAMERA) {
            glm::vec3 scale = transform.scale;
            if (ImGui::DragFloat3("Scale", &scale[0], 0.01f)) {
                if (transform.scaleIsUniform) {
                    for (int i = 0; i < 3; i++) {
                        if (scale[i] == transform.scale[i])
                            continue;
                        scale = glm::vec3(1) * scale[i];
                        break;
                    }
                }
                c = true;
            }
            if (ImGui::Checkbox("Uniform Scale", &transform.scaleIsUniform)) {
                if (transform.scaleIsUniform)
                    scale = glm::vec3(1) * scale.x;
                c = true;
            }
            transform.scale = scale;
        }
    }

    switch (entity->type) {
        case ENTITY_TYPE_ROOT: {
            auto root = static_cast<Root*>(entity);
            ImGui::DragFloat("Scattering Rate", &root->scatterRate, 0.001f, 0.00001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            break;
        }
        case ENTITY_TYPE_CAMERA: {
            CameraInspector(context, static_cast<Camera*>(entity));
            break;
        }
        case ENTITY_TYPE_MESH_INSTANCE: {
            auto instance = static_cast<MeshInstance*>(entity);
            c |= ResourceSelectorDropDown("Mesh", scene.meshes, &instance->mesh);
            ImGui::Spacing();
            MeshInspector(context, instance->mesh, true);
            break;
        }
        case ENTITY_TYPE_PLANE: {
            auto plane = static_cast<Plane*>(entity);
            c |= ResourceSelectorDropDown("Material", scene.materials, &plane->material);
            ImGui::Spacing();
            MaterialInspector(context, plane->material, true);
            break;
        }
        case ENTITY_TYPE_SPHERE: {
            auto sphere = static_cast<Sphere*>(entity);
            c |= ResourceSelectorDropDown("Material", scene.materials, &sphere->material);
            ImGui::Spacing();
            MaterialInspector(context, sphere->material, true);
            break;
        }
        case ENTITY_TYPE_CUBE: {
            auto cube = static_cast<Cube*>(entity);
            c |= ResourceSelectorDropDown("Material", scene.materials, &cube->material);
            ImGui::Spacing();
            MaterialInspector(context, cube->material, true);
            break;
        }
    }

    if (c) scene.dirtyFlags |= SCENE_DIRTY_OBJECTS;

    ImGui::PopID();
}

static void EntityTreeNode(UIContext* context, Entity* entity)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (entity->children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;

    if (entity->type == ENTITY_TYPE_ROOT)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    if (context->selectionType == SELECTION_TYPE_ENTITY && context->entity == entity)
        flags |= ImGuiTreeNodeFlags_Selected;

    if (!entity->active) {
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.x *= 0.5f;
        color.y *= 0.5f;
        color.z *= 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
    }

    if (ImGui::TreeNodeEx(entity->name.c_str(), flags)) {
        if (ImGui::IsItemClicked()) {
            context->selectionType = SELECTION_TYPE_ENTITY;
            context->entity = entity;
        }

        if (ImGui::BeginPopupContextItem()) {
            for (int k = 0; k < ENTITY_TYPE__COUNT; k++) {
                if (k == ENTITY_TYPE_ROOT) continue;
                char buffer[256];
                auto type = static_cast<EntityType>(k);
                snprintf(buffer, std::size(buffer), "Create %s...", EntityTypeName(type));
                if (ImGui::MenuItem(buffer)) {
                    auto child = CreateEntity(context->scene, type, entity);
                    child->name = std::format("New {}", EntityTypeName(type));
                    context->scene->dirtyFlags |= SCENE_DIRTY_OBJECTS;
                    context->selectionType = SELECTION_TYPE_ENTITY;
                    context->entity = child;
                }
            }
            ImGui::EndPopup();
        }

        for (Entity* child : entity->children)
            EntityTreeNode(context, child);

        ImGui::TreePop();
    }

    if (!entity->active) {
        ImGui::PopStyleColor();
    }
}

void ShowResourcesWindow(UIContext* context)
{
    bool c = false;

    Scene& scene = *context->scene;

    ImGui::Begin("Resources");

    // Textures
    {
        auto getter = [](void* context, int index) {
            auto scene = static_cast<Scene*>(context);
            if (index < 0 || index >= scene->textures.size())
                return "";
            return scene->textures[index]->name.c_str();
        };

        int itemIndex = -1;
        int itemCount = static_cast<int>(scene.textures.size());

        if (context->selectionType == SELECTION_TYPE_TEXTURE) {
            for (int k = 0; k < itemCount; k++)
                if (scene.textures[k] == context->texture)
                    itemIndex = k;
        }

        if (ImGui::ListBox("Textures", &itemIndex, getter, &scene, itemCount, 6)) {
            context->selectionType = SELECTION_TYPE_TEXTURE;
            context->texture = scene.textures[itemIndex];
        }
    }

    // Materials
    {
        auto getter = [](void* context, int index) {
            auto scene = static_cast<Scene*>(context);
            if (index < 0 || index >= scene->materials.size())
                return "";
            return scene->materials[index]->name.c_str();
        };

        int itemIndex = -1;
        int itemCount = static_cast<int>(scene.materials.size());

        if (context->selectionType == SELECTION_TYPE_MATERIAL) {
            for (int k = 0; k < itemCount; k++)
                if (scene.materials[k] == context->material)
                    itemIndex = k;
        }

        if (ImGui::ListBox("Materials", &itemIndex, getter, &scene, itemCount, 6)) {
            context->selectionType = SELECTION_TYPE_MATERIAL;
            context->material = scene.materials[itemIndex];
        }
    }

    // Meshes
    {
        auto getter = [](void* context, int index) {
            auto scene = static_cast<Scene*>(context);
            if (index < 0 || index >= scene->meshes.size())
                return "";
            return scene->meshes[index]->name.c_str();
        };

        int itemIndex = -1;
        int itemCount = static_cast<int>(scene.meshes.size());

        if (context->selectionType == SELECTION_TYPE_MESH) {
            for (int k = 0; k < itemCount; k++)
                if (scene.meshes[k] == context->mesh)
                    itemIndex = k;
        }

        if (ImGui::ListBox("Meshes", &itemIndex, getter, &scene, itemCount, 6)) {
            context->selectionType = SELECTION_TYPE_MESH;
            context->mesh = scene.meshes[itemIndex];
        }
    }

    ImGui::End();
}

void ShowSceneHierarchyWindow(UIContext* context)
{
    Scene& scene = *context->scene;

    ImGui::Begin("Scene Hierarchy");

    EntityTreeNode(context, &scene.root);

    ImGui::End();
}

void ShowInspectorWindow(UIContext* context)
{
    ImGui::Begin("Inspector");
    ImGui::PushItemWidth(0.50f * ImGui::GetWindowWidth());

    Scene& scene = *context->scene;

    switch (context->selectionType) {
        case SELECTION_TYPE_TEXTURE:
            TextureInspector(context, context->texture);
            break;
        case SELECTION_TYPE_MATERIAL:
            MaterialInspector(context, context->material);
            break;
        case SELECTION_TYPE_MESH:
            MeshInspector(context, context->mesh);
            break;
        case SELECTION_TYPE_ENTITY:
            EntityInspector(context, context->entity);
            break;
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border]                 = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button]                 = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator]              = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding                     = ImVec2(8.00f, 8.00f);
    style.FramePadding                      = ImVec2(10.00f, 4.00f);
    style.CellPadding                       = ImVec2(6.00f, 6.00f);
    style.ItemSpacing                       = ImVec2(3.00f, 3.00f);
    style.ItemInnerSpacing                  = ImVec2(3.00f, 3.00f);
    style.TouchExtraPadding                 = ImVec2(0.00f, 0.00f);
    style.IndentSpacing                     = 25;
    style.ScrollbarSize                     = 15;
    style.GrabMinSize                       = 10;
    style.WindowBorderSize                  = 1;
    style.ChildBorderSize                   = 1;
    style.PopupBorderSize                   = 1;
    style.FrameBorderSize                   = 1;
    style.TabBorderSize                     = 1;
    style.WindowRounding                    = 7;
    style.ChildRounding                     = 4;
    style.FrameRounding                     = 3;
    style.PopupRounding                     = 4;
    style.ScrollbarRounding                 = 9;
    style.GrabRounding                      = 3;
    style.LogSliderDeadzone                 = 4;
    style.TabRounding                       = 4;
}
