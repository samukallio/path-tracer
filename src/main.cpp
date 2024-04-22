#include <stdio.h>

#include "vulkan.h"
#include "scene.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

int const WINDOW_WIDTH = 2048;
int const WINDOW_HEIGHT = 1024;
char const* APPLICATION_NAME = "Path Tracer";

struct FrameState
{
    double          time;
    double          mouseX;
    double          mouseY;
};

struct EditorCamera
{
    glm::vec3       position;
    glm::vec3       velocity;
    glm::vec3       rotation;
};

struct RenderCamera
{
    RenderMode      renderMode;
    uint32_t        bounceLimit;

    ToneMappingMode toneMappingMode;
    float           toneMappingWhiteLevel;

    CameraType      type;
    glm::vec3       position;
    glm::vec3       velocity;
    glm::vec3       rotation;
    float           focalLengthInMM;
    float           apertureRadiusInMM;
    float           focusDistance;
};

enum SelectionType
{
    SELECTION_TYPE_NONE         = 0,
    SELECTION_TYPE_TEXTURE      = 1,
    SELECTION_TYPE_MATERIAL     = 2,
    SELECTION_TYPE_MESH         = 3,
    SELECTION_TYPE_OBJECT       = 4,
};

struct Selection
{
    SelectionType   type        = SELECTION_TYPE_NONE;
    Texture*        texture     = nullptr;
    Material*       material    = nullptr;
    Mesh*           mesh        = nullptr;
    SceneObject*    object      = nullptr;
};

struct AppContext
{
    GLFWwindow*     window = nullptr;
    VulkanContext*  vulkan = nullptr;
    Scene           scene;
    FrameState      frames[2];
    uint32_t        frameIndex = 0;

    bool            accumulateSamples;

    EditorCamera    editorCamera;
    RenderCamera    renderCamera;

    bool            renderCameraPossessed;

    Selection       selection;

    double          mouseScrollPosition;
};



AppContext app;

float RepeatRange(float value, float min, float max)
{
    float range = max - min;
    return min + range * glm::fract((value + min) / range);
}

static bool ImGui_DragEulerAngles(const char* label, glm::vec3* angles)
{
    float degrees[3];
    for (int k = 0; k < 3; k++)
        degrees[k] = RepeatRange((*angles)[k], -PI, +PI) * (360 / TAU);
    bool changed = ImGui::DragFloat3(label, degrees);
    for (int k = 0; k < 3; k++)
        (*angles)[k] = RepeatRange(degrees[k] * (TAU / 360), -PI, +PI);
    return changed;
}

bool ShowEditorCameraWindow(EditorCamera& camera)
{
    bool c = false;

    ImGui::Begin("Editor Camera");

    c |= ImGui::InputFloat3("Position", &camera.position[0], "%.3f");
    c |= ImGui_DragEulerAngles("Rotation", &camera.rotation);

    ImGui::End();

    return c;
}

bool ShowRenderCameraWindow(RenderCamera& camera)
{
    bool c = false;

    ImGui::Begin("Render Camera");

    ImGui::SeparatorText("General");

    c |= ImGui::Checkbox("Accumulate Samples", &app.accumulateSamples);

    ImGui::Checkbox("Possess", &app.renderCameraPossessed);

    c |= ImGui::InputFloat3("Position", &camera.position[0], "%.3f");
    c |= ImGui_DragEulerAngles("Rotation", &camera.rotation);

    ImGui::SeparatorText("Render Mode");
    c |= ImGui::RadioButton("Path Tracing", (int*)&camera.renderMode, RENDER_MODE_PATH_TRACE);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Base Color", (int*)&camera.renderMode, RENDER_MODE_BASE_COLOR);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Normal", (int*)&camera.renderMode, RENDER_MODE_NORMAL);
    c |= ImGui::RadioButton("Material ID", (int*)&camera.renderMode, RENDER_MODE_MATERIAL_INDEX);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Primitive ID", (int*)&camera.renderMode, RENDER_MODE_PRIMITIVE_INDEX);

    int bounceLimit = static_cast<int>(camera.bounceLimit);
    c |= ImGui::InputInt("Bounce Limit", &bounceLimit);
    camera.bounceLimit = std::max(1, bounceLimit);

    // Tone mapping operators.  Note that since tone mapping happens as
    // a post-process operation, there is no need to reset the accumulated
    // samples.
    ImGui::SeparatorText("Tone Mapping");
    ImGui::RadioButton("Clamp", (int*)&camera.toneMappingMode, TONE_MAPPING_MODE_CLAMP);
    ImGui::SameLine();
    ImGui::RadioButton("Reinhard", (int*)&camera.toneMappingMode, TONE_MAPPING_MODE_REINHARD);
    ImGui::SameLine();
    ImGui::RadioButton("Hable", (int*)&camera.toneMappingMode, TONE_MAPPING_MODE_HABLE);
    ImGui::RadioButton("ACES", (int*)&camera.toneMappingMode, TONE_MAPPING_MODE_ACES);

    if (camera.toneMappingMode == TONE_MAPPING_MODE_REINHARD) {
        ImGui::SliderFloat("White Level", &camera.toneMappingWhiteLevel, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    ImGui::SeparatorText("Camera");
    c |= ImGui::RadioButton("Pinhole", (int*)&camera.type, CAMERA_TYPE_PINHOLE);
    ImGui::SameLine();
    c |= ImGui::RadioButton("Thin Lens", (int*)&camera.type, CAMERA_TYPE_THIN_LENS);
    ImGui::SameLine();
    c |= ImGui::RadioButton("360", (int*)&camera.type, CAMERA_TYPE_360);

    if (camera.type == CAMERA_TYPE_PINHOLE) {
    }

    if (camera.type == CAMERA_TYPE_THIN_LENS) {
        c |= ImGui::SliderFloat("Focal Length (mm)", &camera.focalLengthInMM, 1.0f, 50.0f);
        c |= ImGui::SliderFloat("Aperture Radius (mm)", &camera.apertureRadiusInMM, 0.01f, 100.0f);
        c |= ImGui::SliderFloat("Focus Distance", &camera.focusDistance, 0.01f, 1000.0f,  "%.3f", ImGuiSliderFlags_Logarithmic);
    }

    ImGui::End();
    
    return c;
}

bool ShowResourcesWindow()
{
    bool c = false;

    ImGui::Begin("Resources");

    Selection& selection = app.selection;
    Scene& scene = app.scene;

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

        if (selection.type == SELECTION_TYPE_TEXTURE) {
            for (int k = 0; k < itemCount; k++)
                if (scene.textures[k] == selection.texture)
                    itemIndex = k;
        }

        if (ImGui::ListBox("Textures", &itemIndex, getter, &scene, itemCount, 6)) {
            selection.type = SELECTION_TYPE_TEXTURE;
            selection.texture = scene.textures[itemIndex];
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

        if (selection.type == SELECTION_TYPE_MATERIAL) {
            for (int k = 0; k < itemCount; k++)
                if (scene.materials[k] == selection.material)
                    itemIndex = k;
        }

        if (ImGui::ListBox("Materials", &itemIndex, getter, &scene, itemCount, 6)) {
            selection.type = SELECTION_TYPE_MATERIAL;
            selection.material = scene.materials[itemIndex];
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

        if (selection.type == SELECTION_TYPE_MESH) {
            for (int k = 0; k < itemCount; k++)
                if (scene.meshes[k] == selection.mesh)
                    itemIndex = k;
        }

        if (ImGui::ListBox("Meshes", &itemIndex, getter, &scene, itemCount, 6)) {
            selection.type = SELECTION_TYPE_MESH;
            selection.mesh = scene.meshes[itemIndex];
        }
    }

    ImGui::End();

    return c;
}

template<typename ResourceT>
bool ResourceSelectorDropDown(
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

    for (int k = 0; k < itemCount; k++)
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

bool TextureInspector(Texture* texture)
{
    Scene& scene = app.scene;

    if (!texture) return false;

    ImGui::SeparatorText("Texture");

    ImGui::InputText("Name", &texture->name);
    ImGui::LabelText("Size", "%u x %u", texture->width, texture->height);

    return false;
}

bool MaterialInspector(Material* material)
{
    Scene& scene = app.scene;

    if (!material) return false;

    ImGui::SeparatorText("Material");

    bool c = false;
    ImGui::InputText("Name", &material->name);
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

    return c;
}

bool MeshInspector(Mesh* mesh)
{
    Scene& scene = app.scene;

    if (!mesh) return false;

    ImGui::SeparatorText("Mesh");
    ImGui::InputText("Name", &mesh->name);

    return false;
}

bool SceneObjectInspector(SceneObject* object)
{
    Scene& scene = app.scene;

    if (!object) return false;

    char const* objectTypeLabel = "(unknown)";
    switch (object->type) {
        case OBJECT_TYPE_MESH_INSTANCE:
            objectTypeLabel = "Mesh Instance";
            break;
        case OBJECT_TYPE_PLANE:
            objectTypeLabel = "Plane";
            break;
        case OBJECT_TYPE_SPHERE:
            objectTypeLabel = "Sphere";
            break;
    }
    ImGui::SeparatorText(objectTypeLabel);

    bool c = false;

    ImGui::InputText("Name", &object->name);

    Transform& transform = object->transform;
    c |= ImGui::DragFloat3("Position", &transform.position[0], 0.1f);
    c |= ImGui_DragEulerAngles("Rotation", &transform.rotation);
    c |= ImGui::DragFloat3("Scale", &transform.scale[0], 0.01f);

    switch (object->type) {
        case OBJECT_TYPE_MESH_INSTANCE: {
            c |= ResourceSelectorDropDown("Mesh", scene.meshes, &object->mesh);
            MeshInspector(object->mesh);
            break;
        }
        case OBJECT_TYPE_PLANE:
        case OBJECT_TYPE_SPHERE: {
            c |= ResourceSelectorDropDown("Material", scene.materials, &object->material);
            break;
        }
    }

    if (c) scene.dirtyFlags |= SCENE_DIRTY_OBJECTS;

    return c;
}

void DoSceneObjectNode(SceneObject* object)
{
    Selection& selection = app.selection;

    ImGuiTreeNodeFlags flags = 0;

    if (object->children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;

    if (selection.type == SELECTION_TYPE_OBJECT && selection.object == object)
        flags |= ImGuiTreeNodeFlags_Selected;

    if (ImGui::TreeNodeEx(object->name.c_str(), flags)) {
        if (ImGui::IsItemClicked()) {
            selection.type = SELECTION_TYPE_OBJECT;
            selection.object = object;
        }

        for (SceneObject* child : object->children)
            DoSceneObjectNode(child);

        ImGui::TreePop();
    }
}

void ShowSceneHierarchyWindow()
{
    Scene& scene = app.scene;

    ImGui::Begin("Scene Hierarchy");

    for (SceneObject* object : scene.objects)
        DoSceneObjectNode(object);

    ImGui::End();
}

bool ShowInspectorWindow()
{
    bool c = false;

    ImGui::Begin("Inspector");

    Scene& scene = app.scene;
    Selection& selection = app.selection;

    switch (selection.type) {
        case SELECTION_TYPE_TEXTURE: {
            TextureInspector(selection.texture);
            break;
        }
        case SELECTION_TYPE_MATERIAL: {
            MaterialInspector(selection.material);
            break;
        }
        case SELECTION_TYPE_MESH: {
            MeshInspector(selection.mesh);
            break;
        }
        case SELECTION_TYPE_OBJECT: {
            bool c = false;
            SceneObject* object = selection.object;
            SceneObjectInspector(object);
            MaterialInspector(object->material);
            if (c) app.scene.dirtyFlags |= SCENE_DIRTY_OBJECTS;
            break;
        }
    }

    ImGui::End();

    return c;
}

void Frame()
{
    FrameState& previous = app.frames[(app.frameIndex + 0) % 2];
    FrameState& current = app.frames[(app.frameIndex + 1) % 2];
    app.frameIndex++;

    // Update time and input.
    current.time = glfwGetTime();
    float deltaTime = static_cast<float>(current.time - previous.time);
    glfwGetCursorPos(app.window, &current.mouseX, &current.mouseY);
    float deltaMouseX = static_cast<float>(current.mouseX - previous.mouseX);
    float deltaMouseY = static_cast<float>(current.mouseY - previous.mouseY);

    ImGuiIO& imGuiIO = ImGui::GetIO();

    // ImGui.
    bool editorCameraChanged = false;
    bool renderCameraChanged = false;
    {
        bool c = false;

        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = deltaTime;
        io.MousePos.x = static_cast<float>(current.mouseX);
        io.MousePos.y = static_cast<float>(current.mouseY);
        io.MouseDown[0] = glfwGetMouseButton(app.window, GLFW_MOUSE_BUTTON_1);
        io.DisplaySize.x = WINDOW_WIDTH;
        io.DisplaySize.y = WINDOW_HEIGHT;
        io.MouseWheel = static_cast<float>(app.mouseScrollPosition);
        app.mouseScrollPosition = 0.0;

        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::ShowDemoWindow();

        editorCameraChanged = ShowEditorCameraWindow(app.editorCamera);
        renderCameraChanged = ShowRenderCameraWindow(app.renderCamera);
        ShowInspectorWindow();
        ShowResourcesWindow();
        ShowSceneHierarchyWindow();

        ImGui::EndFrame();
        ImGui::Render();
    }

    // Handle camera movement.
    bool cameraMoved = false;
    {
        bool editing = !app.renderCameraPossessed;
        glm::vec3& position = editing ? app.editorCamera.position : app.renderCamera.position;
        glm::vec3& velocity = editing ? app.editorCamera.velocity : app.renderCamera.velocity;
        glm::vec3& rotation = editing ? app.editorCamera.rotation : app.renderCamera.rotation;
        bool changed = editing ? editorCameraChanged : renderCameraChanged;

        glm::vec3 forward = glm::quat(rotation) * glm::vec3(1, 0, 0);

        if (!imGuiIO.WantCaptureMouse && glfwGetMouseButton(app.window, GLFW_MOUSE_BUTTON_2)) {
            glm::vec3 delta {};
            if (glfwGetKey(app.window, GLFW_KEY_A))
                delta -= glm::cross(forward, glm::vec3(0, 0, 1));
            if (glfwGetKey(app.window, GLFW_KEY_D))
                delta += glm::cross(forward, glm::vec3(0, 0, 1));
            if (glfwGetKey(app.window, GLFW_KEY_W))
                delta += forward;
            if (glfwGetKey(app.window, GLFW_KEY_S))
                delta -= forward;
            if (glm::length(delta) > 0)
                velocity = 2.0f * glm::normalize(delta);

            rotation.z -= deltaMouseX * 0.01f;
            rotation.z = RepeatRange(rotation.z, -PI, +PI);
            rotation.y += deltaMouseY * 0.01f;
            rotation.y = glm::clamp(rotation.y, -0.45f * PI, +0.45f * PI);
            changed = true;
        }

        position += deltaTime * velocity;
        velocity *= expf(-deltaTime / 0.05f);

        if (glm::length(velocity) > 0)
            changed = true;

        if (glm::length(velocity) < 1e-2f)
            velocity = glm::vec3(0);

        if (changed) cameraMoved = true;
    }

    if (app.renderCameraPossessed) {
        app.editorCamera.position = app.renderCamera.position;
        app.editorCamera.rotation = app.renderCamera.rotation;

        if (cameraMoved)
            renderCameraChanged = true;
        if (app.scene.dirtyFlags != 0)
            renderCameraChanged = true;
    }

    FrameUniformBuffer uniforms = {
        .frameRandomSeed = app.frameIndex,
        .sceneObjectCount = static_cast<uint32_t>(app.scene.objects.size()),
        .renderBounceLimit = app.renderCamera.bounceLimit,
        .toneMappingMode = app.renderCamera.toneMappingMode,
        .toneMappingWhiteLevel = app.renderCamera.toneMappingWhiteLevel,
    };

    if (!app.renderCameraPossessed) {
        EditorCamera& camera = app.editorCamera;

        glm::vec3 forward = glm::quat(camera.rotation) * glm::vec3(1, 0, 0);
        glm::mat4 viewMatrix = glm::lookAt(camera.position - forward * 2.0f, camera.position, glm::vec3(0, 0, 1));
        glm::mat4 worldMatrix = glm::inverse(viewMatrix);

        if (!imGuiIO.WantCaptureMouse && glfwGetMouseButton(app.window, GLFW_MOUSE_BUTTON_1)) {
            glm::vec2 sensorSize = { 0.032f, 0.018f };

            glm::vec2 samplePositionNormalized = {
                current.mouseX / WINDOW_WIDTH,
                current.mouseY / WINDOW_HEIGHT
            };

            glm::vec3 sensorPositionNormalized = {
                -sensorSize.x * (samplePositionNormalized.x - 0.5),
                -sensorSize.y * (0.5 - samplePositionNormalized.y),
                0.020f
            };

            glm::vec3 rayVector = -sensorPositionNormalized;

            Ray ray;
            ray.origin = (worldMatrix * glm::vec4(0, 0, 0, 1)).xyz;
            ray.direction = glm::normalize(worldMatrix * glm::vec4(rayVector, 0)).xyz;

            Hit hit;
            if (Trace(&app.scene, ray, hit)) {
                app.selection.type = SELECTION_TYPE_OBJECT;
                for (SceneObject* object : app.scene.objects)
                    if (object->packedObjectIndex == hit.objectIndex)
                        app.selection.object = object;
            }
        }

        uniforms.renderMode = RENDER_MODE_EDIT;
        uniforms.cameraType = CAMERA_TYPE_PINHOLE;
        uniforms.cameraSensorDistance = 0.020f;
        uniforms.cameraSensorSize = { 0.032f, 0.018f };
        uniforms.cameraWorldMatrix = glm::inverse(viewMatrix);
        uniforms.renderFlags = 0;

        if (app.selection.type == SELECTION_TYPE_OBJECT)
            uniforms.highlightObjectIndex = app.selection.object->packedObjectIndex;
        else
            uniforms.highlightObjectIndex = 0xFFFFFFFF;
    }
    else {
        RenderCamera& camera = app.renderCamera;

        float sensorDistance = 1.0f / (1000.0f / camera.focalLengthInMM - 1.0f / camera.focusDistance);

        glm::vec3 origin = camera.position;
        glm::vec3 forward = glm::quat(camera.rotation) * glm::vec3(1, 0, 0);
        glm::mat4 viewMatrix = glm::lookAt(origin - forward * 2.0f, origin, glm::vec3(0, 0, 1));
        glm::mat4 worldMatrix = glm::inverse(viewMatrix);

        uniforms.renderMode = camera.renderMode;
        uniforms.cameraType = camera.type;
        uniforms.cameraFocalLength = camera.focalLengthInMM / 1000.0f;
        uniforms.cameraApertureRadius = camera.apertureRadiusInMM / 1000.0f;
        uniforms.cameraSensorDistance = sensorDistance;
        uniforms.cameraSensorSize = { 0.032f, 0.018f };
        uniforms.cameraWorldMatrix = worldMatrix;
        uniforms.highlightObjectIndex = 0xFFFFFFFF;
        uniforms.renderFlags = RENDER_FLAG_SAMPLE_JITTER;

        if (app.accumulateSamples && !renderCameraChanged)
            uniforms.renderFlags |= RENDER_FLAG_ACCUMULATE;
    }

    uint32_t dirtyFlags = BakeSceneData(&app.scene);
    UploadScene(app.vulkan, &app.scene, dirtyFlags);

    RenderFrame(app.vulkan, &uniforms, ImGui::GetDrawData());
}

void ScrollCallback(GLFWwindow* window, double x, double y)
{
    app.mouseScrollPosition += y;
}

static ImGuiKey ImGuiKeyFromGlfwKey(int key)
{
    switch (key) {
    case GLFW_KEY_TAB: return ImGuiKey_Tab;
    case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
    case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
    case GLFW_KEY_UP: return ImGuiKey_UpArrow;
    case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
    case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
    case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
    case GLFW_KEY_HOME: return ImGuiKey_Home;
    case GLFW_KEY_END: return ImGuiKey_End;
    case GLFW_KEY_INSERT: return ImGuiKey_Insert;
    case GLFW_KEY_DELETE: return ImGuiKey_Delete;
    case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
    case GLFW_KEY_SPACE: return ImGuiKey_Space;
    case GLFW_KEY_ENTER: return ImGuiKey_Enter;
    case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
    case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
    case GLFW_KEY_COMMA: return ImGuiKey_Comma;
    case GLFW_KEY_MINUS: return ImGuiKey_Minus;
    case GLFW_KEY_PERIOD: return ImGuiKey_Period;
    case GLFW_KEY_SLASH: return ImGuiKey_Slash;
    case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
    case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
    case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
    case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
    case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
    case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
    case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
    case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
    case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
    case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
    case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
    case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
    case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
    case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
    case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
    case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
    case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
    case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
    case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
    case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
    case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
    case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
    case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
    case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
    case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
    case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
    case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
    case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
    case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
    case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
    case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
    case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
    case GLFW_KEY_MENU: return ImGuiKey_Menu;
    case GLFW_KEY_0: return ImGuiKey_0;
    case GLFW_KEY_1: return ImGuiKey_1;
    case GLFW_KEY_2: return ImGuiKey_2;
    case GLFW_KEY_3: return ImGuiKey_3;
    case GLFW_KEY_4: return ImGuiKey_4;
    case GLFW_KEY_5: return ImGuiKey_5;
    case GLFW_KEY_6: return ImGuiKey_6;
    case GLFW_KEY_7: return ImGuiKey_7;
    case GLFW_KEY_8: return ImGuiKey_8;
    case GLFW_KEY_9: return ImGuiKey_9;
    case GLFW_KEY_A: return ImGuiKey_A;
    case GLFW_KEY_B: return ImGuiKey_B;
    case GLFW_KEY_C: return ImGuiKey_C;
    case GLFW_KEY_D: return ImGuiKey_D;
    case GLFW_KEY_E: return ImGuiKey_E;
    case GLFW_KEY_F: return ImGuiKey_F;
    case GLFW_KEY_G: return ImGuiKey_G;
    case GLFW_KEY_H: return ImGuiKey_H;
    case GLFW_KEY_I: return ImGuiKey_I;
    case GLFW_KEY_J: return ImGuiKey_J;
    case GLFW_KEY_K: return ImGuiKey_K;
    case GLFW_KEY_L: return ImGuiKey_L;
    case GLFW_KEY_M: return ImGuiKey_M;
    case GLFW_KEY_N: return ImGuiKey_N;
    case GLFW_KEY_O: return ImGuiKey_O;
    case GLFW_KEY_P: return ImGuiKey_P;
    case GLFW_KEY_Q: return ImGuiKey_Q;
    case GLFW_KEY_R: return ImGuiKey_R;
    case GLFW_KEY_S: return ImGuiKey_S;
    case GLFW_KEY_T: return ImGuiKey_T;
    case GLFW_KEY_U: return ImGuiKey_U;
    case GLFW_KEY_V: return ImGuiKey_V;
    case GLFW_KEY_W: return ImGuiKey_W;
    case GLFW_KEY_X: return ImGuiKey_X;
    case GLFW_KEY_Y: return ImGuiKey_Y;
    case GLFW_KEY_Z: return ImGuiKey_Z;
    case GLFW_KEY_F1: return ImGuiKey_F1;
    case GLFW_KEY_F2: return ImGuiKey_F2;
    case GLFW_KEY_F3: return ImGuiKey_F3;
    case GLFW_KEY_F4: return ImGuiKey_F4;
    case GLFW_KEY_F5: return ImGuiKey_F5;
    case GLFW_KEY_F6: return ImGuiKey_F6;
    case GLFW_KEY_F7: return ImGuiKey_F7;
    case GLFW_KEY_F8: return ImGuiKey_F8;
    case GLFW_KEY_F9: return ImGuiKey_F9;
    case GLFW_KEY_F10: return ImGuiKey_F10;
    case GLFW_KEY_F11: return ImGuiKey_F11;
    case GLFW_KEY_F12: return ImGuiKey_F12;
    case GLFW_KEY_F13: return ImGuiKey_F13;
    case GLFW_KEY_F14: return ImGuiKey_F14;
    case GLFW_KEY_F15: return ImGuiKey_F15;
    case GLFW_KEY_F16: return ImGuiKey_F16;
    case GLFW_KEY_F17: return ImGuiKey_F17;
    case GLFW_KEY_F18: return ImGuiKey_F18;
    case GLFW_KEY_F19: return ImGuiKey_F19;
    case GLFW_KEY_F20: return ImGuiKey_F20;
    case GLFW_KEY_F21: return ImGuiKey_F21;
    case GLFW_KEY_F22: return ImGuiKey_F22;
    case GLFW_KEY_F23: return ImGuiKey_F23;
    case GLFW_KEY_F24: return ImGuiKey_F24;
    default: return ImGuiKey_None;
    }
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;

    ImGuiIO& io = ImGui::GetIO();

    ImGuiKey imKey = ImGuiKeyFromGlfwKey(key);

    switch (imKey) {
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
        io.AddKeyEvent(ImGuiMod_Shift, action == GLFW_PRESS);
        break;

    default:
        io.AddKeyEvent(imKey, action == GLFW_PRESS);
        break;
    }
}

static void CharCallback(GLFWwindow* window, unsigned int codepoint)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharacter(codepoint);
}

int main()
{
    Scene& scene = app.scene;

    Material* material = CreateMaterial(&scene, "viking_room");
    material->baseColorTexture = LoadTexture(&scene, "../scene/viking_room.png");

    LoadModelOptions options;
    options.directoryPath = "../scene";
    options.defaultMaterial = material;
    Mesh* mesh = LoadModel(&scene, "../scene/viking_room.obj", &options);

    scene.objects.push_back(new SceneObject {
        .name = "room",
        .transform = {
            .position = glm::vec3(0, 0, 0),
            .rotation = glm::vec3(0, 0, 0),
        },
        .type = OBJECT_TYPE_MESH_INSTANCE,
        .mesh = mesh,
    });

    Material* glass = CreateMaterial(&scene, "glass");
    glass->refraction = 1.0f;
    glass->refractionIndex = 1.5f;

    scene.objects.push_back(new SceneObject {
        .name = "sphere",
        .transform = {
            .position = glm::vec3(0, 0, 0),
            .rotation = glm::vec3(0, 0, 0),
        },
        .type = OBJECT_TYPE_SPHERE,
        .material = glass,
    });

    scene.objects.push_back(new SceneObject {
        .name = "plane",
        .transform = {
            .position = glm::vec3(0, 0, 0),
            .rotation = glm::vec3(0, 0, 0),
        },
        .type = OBJECT_TYPE_PLANE,
    });

    BakeSceneData(&scene);
    LoadSkybox(&scene, "../scene/CloudedSunGlow4k.hdr");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    app.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);
    app.vulkan = CreateVulkan(app.window, APPLICATION_NAME);

    UploadScene(app.vulkan, &scene, SCENE_DIRTY_ALL);

    app.editorCamera.position = { 0, 0, 0 };
    app.editorCamera.velocity = { 0, 0, 0 };
    app.editorCamera.rotation = { 0, 0, 0 };

    app.renderCamera.renderMode = RENDER_MODE_NORMAL;
    app.renderCamera.type = CAMERA_TYPE_PINHOLE;
    app.renderCamera.position = { 0, 0, 0 };
    app.renderCamera.rotation = { 0, 0, 0 };
    app.renderCamera.bounceLimit = 5;
    app.renderCamera.focusDistance = 1.0f;
    app.renderCamera.focalLengthInMM = 20.0f;
    app.renderCamera.apertureRadiusInMM = 40.0f;

    FrameState& initial = app.frames[0];
    initial.time = glfwGetTime();
    glfwGetCursorPos(app.window, &initial.mouseX, &initial.mouseY);
    glfwSetScrollCallback(app.window, ScrollCallback);
    glfwSetKeyCallback(app.window, KeyCallback);
    glfwSetCharCallback(app.window, CharCallback);

    while (!glfwWindowShouldClose(app.window)) {
        glfwPollEvents();
        Frame();
    }

    DestroyVulkan(app.vulkan);

    glfwDestroyWindow(app.window);
    glfwTerminate();

    ImGui::DestroyContext();
}
