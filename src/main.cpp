#include <stdio.h>

#include <format>

#include "vulkan.h"
#include "scene.h"
#include "ui.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

int const WINDOW_WIDTH = 1920;
int const WINDOW_HEIGHT = 1080;
char const* APPLICATION_NAME = "Path Tracer";

Application app;

void Frame()
{
    ImGuiIO& io = ImGui::GetIO();

    // ImGui.
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::ShowDemoWindow();
    InspectorWindow(&app);
    ResourceBrowserWindow(&app);
    SceneHierarchyWindow(&app);
    ImGui::EndFrame();

    ImGui::Render();

    // Handle camera movement.
    {
        bool editing = !app.camera;
        glm::vec3& position = editing ? app.editorCamera.position : app.camera->transform.position;
        glm::vec3& velocity = editing ? app.editorCamera.velocity : app.camera->velocity;
        glm::vec3& rotation = editing ? app.editorCamera.rotation : app.camera->transform.rotation;
        bool moved = false;

        glm::vec3 forward = glm::quat(rotation) * glm::vec3(1, 0, 0);

        if (!io.WantCaptureMouse && io.MouseDown[1]) {
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

            rotation.z -= io.MouseDelta.x * 0.01f;
            rotation.z = RepeatRange(rotation.z, -PI, +PI);
            rotation.y += io.MouseDelta.y * 0.01f;
            rotation.y = glm::clamp(rotation.y, -0.45f * PI, +0.45f * PI);
            moved = true;
        }

        position += io.DeltaTime * velocity;
        velocity *= expf(-io.DeltaTime / 0.05f);

        if (glm::length(velocity) > 0)
            moved = true;

        if (glm::length(velocity) < 1e-2f)
            velocity = glm::vec3(0);

        if (moved && app.camera) {
            app.scene->dirtyFlags |= SCENE_DIRTY_CAMERAS;
            app.editorCamera.position = app.camera->transform.position;
            app.editorCamera.rotation = app.camera->transform.rotation;
        }
    }

    FrameUniformBuffer uniforms = {
        .frameRandomSeed = app.frameIndex,
        .sceneScatterRate = app.scene->root.scatterRate,
        .skyboxDistributionFrame = app.scene->skyboxDistributionFrame,
        .skyboxDistributionConcentration = app.scene->skyboxDistributionConcentration,
        .skyboxBrightness = app.scene->root.skyboxBrightness,
        .skyboxWhiteFurnace = app.scene->root.skyboxWhiteFurnace,
    };

    if (!app.camera) {
        EditorCamera& camera = app.editorCamera;

        glm::vec3 forward = glm::quat(camera.rotation) * glm::vec3(1, 0, 0);
        glm::mat4 viewMatrix = glm::lookAt(camera.position - forward * 2.0f, camera.position, glm::vec3(0, 0, 1));
        glm::mat4 worldMatrix = glm::inverse(viewMatrix);

        if (!io.WantCaptureMouse && io.MouseDown[0]) {
            glm::vec2 sensorSize = { 0.032f, 0.018f };

            glm::vec2 samplePositionNormalized = {
                io.MousePos.x / WINDOW_WIDTH,
                io.MousePos.y / WINDOW_HEIGHT
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
            if (Trace(app.scene, ray, hit)) {
                for (Entity* entity : app.scene->root.children) {
                    if (entity->packedObjectIndex == hit.objectIndex) {
                        app.selectedEntity = entity;
                        app.selectionType = SELECTION_TYPE_ENTITY;
                    }
                }
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
        uniforms.brightness = 1.0f;
        uniforms.toneMappingMode = TONE_MAPPING_MODE_CLAMP;
        uniforms.toneMappingWhiteLevel = 1.0f;

        if (app.selectionType == SELECTION_TYPE_ENTITY)
            uniforms.highlightObjectIndex = app.selectedEntity->packedObjectIndex;
        else
            uniforms.highlightObjectIndex = OBJECT_INDEX_NONE;
    }
    else {
        Camera* camera = app.camera;

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
        uniforms.highlightObjectIndex = OBJECT_INDEX_NONE;
        uniforms.renderFlags = RENDER_FLAG_SAMPLE_JITTER;
        uniforms.renderSampleBlockSize = 1u << app.camera->renderSampleBlockSizeLog2;
        uniforms.renderBounceLimit = app.camera->renderBounceLimit;
        uniforms.renderTerminationProbability = app.camera->renderTerminationProbability;
        uniforms.renderMeshComplexityScale = app.camera->renderMeshComplexityScale;
        uniforms.renderSceneComplexityScale = app.camera->renderSceneComplexityScale;
        uniforms.brightness = app.camera->brightness;
        uniforms.toneMappingMode = app.camera->toneMappingMode;
        uniforms.toneMappingWhiteLevel = app.camera->toneMappingWhiteLevel;
        uniforms.renderFlags = app.camera->renderFlags;

        if (app.scene->dirtyFlags != 0)
            uniforms.renderFlags &= ~RENDER_FLAG_ACCUMULATE;
    }

    uint32_t dirtyFlags = PackSceneData(app.scene);
    UploadScene(app.vulkan, app.scene, dirtyFlags);

    uniforms.sceneObjectCount = static_cast<uint32_t>(app.scene->sceneObjectPack.size());

    RenderFrame(app.vulkan, &uniforms, ImGui::GetDrawData());
}

static void MouseButtonInputCallback(GLFWwindow* window, int button, int action, int mods)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, action == GLFW_PRESS);
}

static void MousePositionInputCallback(GLFWwindow* window, double x, double y)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
}

static void MouseScrollInputCallback(GLFWwindow* window, double x, double y)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(static_cast<float>(x), static_cast<float>(y));
}

static void KeyInputCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;

    ImGuiIO& io = ImGui::GetIO();
    
    ImGuiKey imKey = [](int key) {
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
    }(key);

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

static void CharacterInputCallback(GLFWwindow* window, unsigned int codepoint)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharacter(codepoint);
}

static void CreateBasicResources(Scene* scene)
{
    struct MetalMaterialData {
        char const* name;
        glm::vec3 baseColor;
        glm::vec3 specularColor;
    };

    MetalMaterialData const metals[] = {
        { "Silver"   , { 0.9868f, 0.9830f, 0.9667f }, { 0.9929f, 0.9961f, 1.0000f } },
        { "Aluminum" , { 0.9157f, 0.9226f, 0.9236f }, { 0.9090f, 0.9365f, 0.9596f } },
        { "Gold"     , { 1.0000f, 0.7099f, 0.3148f }, { 0.9408f, 0.9636f, 0.9099f } },
        { "Chromium" , { 0.5496f, 0.5561f, 0.5531f }, { 0.7372f, 0.7511f, 0.8170f } },
        { "Copper"   , { 1.0000f, 0.6504f, 0.5274f }, { 0.9755f, 0.9349f, 0.9301f } },
        { "Iron"     , { 0.8951f, 0.8755f, 0.8154f }, { 0.8551f, 0.8800f, 0.8966f } },
        { "Mercury"  , { 0.7815f, 0.7795f, 0.7783f }, { 0.8103f, 0.8532f, 0.9046f } },
        { "Magnesium", { 0.8918f, 0.8821f, 0.8948f }, { 0.8949f, 0.9147f, 0.9504f } },
        { "Nickel"   , { 0.7014f, 0.6382f, 0.5593f }, { 0.8134f, 0.8352f, 0.8725f } },
        { "Palladium", { 0.7363f, 0.7023f, 0.6602f }, { 0.8095f, 0.8369f, 0.8739f } },
        { "Platinum" , { 0.9602f, 0.9317f, 0.8260f }, { 0.9501f, 0.9461f, 0.9352f } },
        { "Titanium" , { 0.4432f, 0.3993f, 0.3599f }, { 0.8627f, 0.9066f, 0.9481f } },
        { "Zinc"     , { 0.8759f, 0.8685f, 0.8542f }, { 0.8769f, 0.9037f, 0.9341f } },
    };

    for (auto const& metal : metals) {
        Material* material = CreateMaterial(scene, metal.name);
        material->baseMetalness = 1.0f;
        material->baseColor = metal.baseColor;
        material->specularColor = metal.specularColor;
        material->specularRoughness = 0.0f;
    }
}

int main()
{
    Scene scene;

    app.scene = &scene;
    app.camera = nullptr;

    scene.root.name = "Root";

    CreateBasicResources(&scene);

    {
        LoadModelOptions options;
        options.name = "xyzrgb_dragon.obj";
        options.directoryPath = "../scene";
        Prefab* prefab = LoadModelAsPrefab(&scene, "../scene/xyzrgb_dragon.obj", &options);
        CreateEntity(&scene, prefab); 
    }

    {
        LoadModelOptions options;
        options.name = "viking_room.obj";
        options.directoryPath = "../scene";
        options.defaultMaterial = CreateMaterial(&scene, "viking_room");
        options.defaultMaterial->baseColorTexture = LoadTexture(&scene, "../scene/viking_room.png", "viking_room.png");
        Prefab* prefab = LoadModelAsPrefab(&scene, "../scene/viking_room.obj", &options);
        CreateEntity(&scene, prefab); 
    }

    //{
    //    float s = 0.01f;
    //    LoadModelOptions options;
    //    options.name = "sponza.obj";
    //    options.directoryPath = "../scene";
    //    options.vertexTransform = { { 0, s, 0, 0 }, { 0, 0, s, 0 }, { s, 0, 0, 0 }, { 0, 0, 0, 1 } };
    //    options.normalTransform = { { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 1, 0, 0, 0 }, { 0, 0, 0, 1 } };
    //    Prefab* prefab = LoadModelAsPrefab(&scene, "../scene/sponza.obj", &options);
    //    CreateEntity(&scene, prefab);
    //}

    for (int k = 0; k < 1; k++) {
        auto sphere = new Sphere;
        sphere->name = std::format("Sphere {}", k+1);
        sphere->material = CreateMaterial(&scene, std::format("Sphere {} Material", k+1).c_str());
        sphere->material->transmissionWeight = 0.0f;
        sphere->material->specularRoughness = 0.0f;
        sphere->material->specularIOR = 1.5f;
        scene.root.children.push_back(sphere);
    }

    auto plane = static_cast<Plane*>(CreateEntity(&scene, ENTITY_TYPE_PLANE));
    plane->name = "Plane";
    plane->material = CreateMaterial(&scene, "Plane Material");
    plane->material->baseColorTexture = CreateCheckerTexture(&scene, "Plane Texture", glm::vec4(1,1,1,1), glm::vec4(0.5,0.5,0.5,1));
    plane->material->baseColorTexture->enableNearestFiltering = true;
    plane->material->specularRoughness = 0.0f;

    //auto cube = new Cube;
    //cube->name = "Cube";
    //cube->material = metal;
    //scene.root.children.push_back(cube);

    InitializeUI(&app);

    PackSceneData(&scene);
    LoadSkybox(&scene, "../scene/CloudedSunGlow4k.hdr");

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

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = WINDOW_WIDTH;
    io.DisplaySize.y = WINDOW_HEIGHT;


    glfwSetMouseButtonCallback(app.window, MouseButtonInputCallback);
    glfwSetCursorPosCallback(app.window, MousePositionInputCallback);
    glfwSetScrollCallback(app.window, MouseScrollInputCallback);
    glfwSetKeyCallback(app.window, KeyInputCallback);
    glfwSetCharCallback(app.window, CharacterInputCallback);

    double previousTime = glfwGetTime();
    while (!glfwWindowShouldClose(app.window)) {
        glfwPollEvents();

        double currentTime = glfwGetTime();
        io.DeltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        Frame();

        app.frameIndex++;
    }

    DestroyVulkan(app.vulkan);

    glfwDestroyWindow(app.window);
    glfwTerminate();

    ImGui::DestroyContext();
}
