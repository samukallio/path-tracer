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

    for (size_t k = 0; k < mesh->materials.size(); k++) {
        ImGui::PushID(static_cast<int>(k));

        char title[32];
        sprintf_s(title, "Material %llu", k);
        c |= ResourceSelectorDropDown(title, scene.materials, &mesh->materials[k]);

        ImGui::Spacing();
        MaterialInspector(context, mesh->materials[k], true);

        ImGui::PopID();
    }

    if (c) scene.dirtyFlags |= SCENE_DIRTY_MESHES;

    ImGui::PopID();
}

static void CameraInspector(UIContext* context, Camera* camera)
{
    bool c = false;

    ImGui::SeparatorText("General");

    c |= ImGui::Checkbox("Accumulate Samples", &camera->accumulateSamples);

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

    ImGui::SeparatorText("Render Mode");
    c |= ImGui::RadioButton("Path Tracing", (int*)&camera->renderMode, RENDER_MODE_PATH_TRACE);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Base Color", (int*)&camera->renderMode, RENDER_MODE_BASE_COLOR);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Normal", (int*)&camera->renderMode, RENDER_MODE_NORMAL);
    c |= ImGui::RadioButton("Material ID", (int*)&camera->renderMode, RENDER_MODE_MATERIAL_INDEX);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Primitive ID", (int*)&camera->renderMode, RENDER_MODE_PRIMITIVE_INDEX);

    int bounceLimit = static_cast<int>(camera->bounceLimit);
    c |= ImGui::InputInt("Bounce Limit", &bounceLimit);
    camera->bounceLimit = std::max(1, bounceLimit);

    // Tone mapping operators.  Note that since tone mapping happens as
    // a post-process operation, there is no need to reset the accumulated
    // samples.
    ImGui::SeparatorText("Tone Mapping");
    ImGui::RadioButton("Clamp", (int*)&camera->toneMappingMode, TONE_MAPPING_MODE_CLAMP);
    ImGui::SameLine();
    ImGui::RadioButton("Reinhard", (int*)&camera->toneMappingMode, TONE_MAPPING_MODE_REINHARD);
    ImGui::SameLine();
    ImGui::RadioButton("Hable", (int*)&camera->toneMappingMode, TONE_MAPPING_MODE_HABLE);
    ImGui::RadioButton("ACES", (int*)&camera->toneMappingMode, TONE_MAPPING_MODE_ACES);

    if (camera->toneMappingMode == TONE_MAPPING_MODE_REINHARD) {
        ImGui::SliderFloat("White Level", &camera->toneMappingWhiteLevel, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    ImGui::SeparatorText("Camera");
    c |= ImGui::RadioButton("Pinhole", (int*)&camera->model, CAMERA_MODEL_PINHOLE);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Thin Lens", (int*)&camera->model, CAMERA_MODEL_THIN_LENS);
    ImGui::SameLine();
    c |= ImGui::RadioButton("360", (int*)&camera->model, CAMERA_MODEL_360);

    if (camera->model == CAMERA_MODEL_PINHOLE) {
    }

    if (camera->model == CAMERA_MODEL_THIN_LENS) {
        c |= ImGui::SliderFloat("Focal Length (mm)", &camera->focalLengthInMM, 1.0f, 50.0f);
        c |= ImGui::SliderFloat("Aperture Radius (mm)", &camera->apertureRadiusInMM, 0.01f, 100.0f);
        c |= ImGui::SliderFloat("Focus Distance", &camera->focusDistance, 0.01f, 1000.0f,  "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    if (c) context->scene->dirtyFlags |= SCENE_DIRTY_CAMERAS;
}

static void EntityInspector(UIContext* context, Entity* entity)
{
    Scene& scene = *context->scene;

    if (!entity) return;

    ImGui::PushID(entity);

    switch (entity->type) {
        case ENTITY_TYPE_ROOT:
            ImGui::SeparatorText("Scene Root");
            break;
        case ENTITY_TYPE_CAMERA:
            ImGui::SeparatorText("Camera");
            break;
        case ENTITY_TYPE_MESH_INSTANCE:
            ImGui::SeparatorText("Mesh Instance");
            break;
        case ENTITY_TYPE_PLANE:
            ImGui::SeparatorText("Plane");
            break;
        case ENTITY_TYPE_SPHERE:
            ImGui::SeparatorText("Sphere");
            break;
        default:
            ImGui::SeparatorText("(unknown)");
            break;
    }

    bool c = false;

    if (entity->type != ENTITY_TYPE_ROOT) {
        ImGui::InputText("Name", &entity->name);

        Transform& transform = entity->transform;
        c |= ImGui::DragFloat3("Position", &transform.position[0], 0.1f);
        c |= DragEulerAngles("Rotation", &transform.rotation);

        if (entity->type != ENTITY_TYPE_CAMERA) {
            c |= ImGui::DragFloat3("Scale", &transform.scale[0], 0.01f);
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

    if (ImGui::TreeNodeEx(entity->name.c_str(), flags)) {
        if (ImGui::IsItemClicked()) {
            context->selectionType = SELECTION_TYPE_ENTITY;
            context->entity = entity;
        }

        for (Entity* child : entity->children)
            EntityTreeNode(context, child);

        ImGui::TreePop();
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

    ImGui::End();
}
