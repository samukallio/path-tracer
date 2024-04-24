#include <stdio.h>

#include "vulkan.h"
#include "scene.h"
#include "ui.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

int const WINDOW_WIDTH = 1920;
int const WINDOW_HEIGHT = 1080;
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

struct AppContext
{
    GLFWwindow*     window = nullptr;
    VulkanContext*  vulkan = nullptr;
    Scene           scene;
    FrameState      frames[2];
    uint32_t        frameIndex = 0;

    EditorCamera    editorCamera;

    UIContext       ui;

    double          mouseScrollPosition;
};

AppContext app;

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

        ShowInspectorWindow(&app.ui);
        ShowResourcesWindow(&app.ui);
        ShowSceneHierarchyWindow(&app.ui);

        ImGui::EndFrame();
        ImGui::Render();
    }

    // Handle camera movement.
    {
        bool editing = !app.ui.camera;
        glm::vec3& position = editing ? app.editorCamera.position : app.ui.camera->transform.position;
        glm::vec3& velocity = editing ? app.editorCamera.velocity : app.ui.camera->velocity;
        glm::vec3& rotation = editing ? app.editorCamera.rotation : app.ui.camera->transform.rotation;
        bool changed = false;

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

        if (changed && app.ui.camera) {
            app.scene.dirtyFlags |= SCENE_DIRTY_CAMERAS;
            app.editorCamera.position = app.ui.camera->transform.position;
            app.editorCamera.rotation = app.ui.camera->transform.rotation;
        }
    }

    FrameUniformBuffer uniforms = {
        .frameRandomSeed = app.frameIndex,
        .sceneScatterRate = app.scene.root.scatterRate,
    };

    if (!app.ui.camera) {
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
                app.ui.selectionType = SELECTION_TYPE_ENTITY;
                for (Entity* entity : app.scene.root.children)
                    if (entity->packedObjectIndex == hit.objectIndex)
                        app.ui.entity = entity;
            }
        }

        uniforms.renderMode = RENDER_MODE_BASE_COLOR_SHADED;
        uniforms.cameraModel = CAMERA_MODEL_PINHOLE;
        uniforms.cameraSensorDistance = 0.020f;
        uniforms.cameraSensorSize = { 0.032f, 0.018f };
        uniforms.cameraApertureRadius = 0.0f;
        uniforms.cameraTransform = {
            .to = worldMatrix,
            .from = viewMatrix,
        };
        uniforms.renderFlags = 0;

        uniforms.renderBounceLimit = 0;
        uniforms.toneMappingMode = TONE_MAPPING_MODE_CLAMP;
        uniforms.toneMappingWhiteLevel = 1.0f;

        if (app.ui.selectionType == SELECTION_TYPE_ENTITY)
            uniforms.highlightObjectIndex = app.ui.entity->packedObjectIndex;
        else
            uniforms.highlightObjectIndex = 0xFFFFFFFF;
    }
    else {
        Camera* camera = app.ui.camera;

        glm::vec3 origin = camera->transform.position;
        glm::vec3 forward = glm::quat(camera->transform.rotation) * glm::vec3(1, 0, 0);
        glm::mat4 viewMatrix = glm::lookAt(origin - forward * 2.0f, origin, glm::vec3(0, 0, 1));
        glm::mat4 worldMatrix = glm::inverse(viewMatrix);

        uniforms.renderMode = camera->renderMode;
        uniforms.cameraModel = camera->cameraModel;

        if (camera->cameraModel == CAMERA_MODEL_PINHOLE) {
            float const ASPECT_RATIO = WINDOW_WIDTH / float(WINDOW_HEIGHT);
            uniforms.cameraApertureRadius = camera->pinhole.apertureDiameterInMM / 2000.0f;
            uniforms.cameraSensorSize.x = 2 * glm::tan(glm::radians(camera->pinhole.fieldOfViewInDegrees / 2));
            uniforms.cameraSensorSize.y = uniforms.cameraSensorSize.x / ASPECT_RATIO;
            uniforms.cameraSensorDistance = 1.0f;
        }

        if (camera->cameraModel == CAMERA_MODEL_THIN_LENS) {
            uniforms.cameraFocalLength = camera->thinLens.focalLengthInMM / 1000.0f;
            uniforms.cameraApertureRadius = camera->thinLens.apertureDiameterInMM / 2000.0f;
            uniforms.cameraSensorDistance = 1.0f / (1000.0f / camera->thinLens.focalLengthInMM - 1.0f / camera->thinLens.focusDistance);
            uniforms.cameraSensorSize = camera->thinLens.sensorSizeInMM / 1000.0f;
        }

        uniforms.cameraTransform = {
            .to = worldMatrix,
            .from = viewMatrix,
        };
        uniforms.highlightObjectIndex = 0xFFFFFFFF;
        uniforms.renderFlags = RENDER_FLAG_SAMPLE_JITTER;
        uniforms.renderSampleBlockSize = 1u << app.ui.camera->renderSampleBlockSizeLog2;
        uniforms.renderBounceLimit = app.ui.camera->renderBounceLimit;
        uniforms.toneMappingMode = app.ui.camera->toneMappingMode;
        uniforms.toneMappingWhiteLevel = app.ui.camera->toneMappingWhiteLevel;
        uniforms.renderFlags = app.ui.camera->renderFlags;

        if (app.scene.dirtyFlags != 0)
            uniforms.renderFlags &= ~RENDER_FLAG_ACCUMULATE;
    }

    uint32_t dirtyFlags = BakeSceneData(&app.scene);
    UploadScene(app.vulkan, &app.scene, dirtyFlags);

    uniforms.sceneObjectCount = static_cast<uint32_t>(app.scene.packedObjects.size());

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

    app.ui.camera = nullptr;
    app.ui.scene = &app.scene;

    scene.root.name = "Root";

    Material* material = CreateMaterial(&scene, "viking_room");
    material->baseColorTexture = LoadTexture(&scene, "../scene/viking_room.png", "viking_room.png");

    LoadModelOptions options;
    options.name = "viking_room.obj";
    options.directoryPath = "../scene";
    options.defaultMaterial = material;
    Mesh* mesh = LoadModel(&scene, "../scene/viking_room.obj", &options);

    auto room = new MeshInstance;
    room->name = "Room";
    room->mesh = mesh;
    scene.root.children.push_back(room);

    Material* glass = CreateMaterial(&scene, "glass");
    glass->refraction = 1.0f;
    glass->refractionIndex = 1.5f;

    auto sphere = new Sphere;
    sphere->name = "Sphere";
    sphere->material = glass;
    scene.root.children.push_back(sphere);

    auto plane = new Plane;
    plane->name = "Plane";
    scene.root.children.push_back(plane);

    BakeSceneData(&scene);
    LoadSkybox(&scene, "../scene/CloudedSunGlow4k.hdr");

    InitializeImGui();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    app.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);
    app.vulkan = CreateVulkan(app.window, APPLICATION_NAME);

    UploadScene(app.vulkan, &scene, SCENE_DIRTY_ALL);

    app.editorCamera.position = { 0, 0, 0 };
    app.editorCamera.velocity = { 0, 0, 0 };
    app.editorCamera.rotation = { 0, 0, 0 };

    auto camera = new Camera;
    camera->name = "Camera";
    scene.root.children.push_back(camera);

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
