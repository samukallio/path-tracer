#include <stdio.h>

#include "vulkan.h"
#include "scene.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

int const WINDOW_WIDTH = 2048;
int const WINDOW_HEIGHT = 1024;
char const* APPLICATION_NAME = "Path Tracer";

struct CameraState
{
    glm::vec3       position;
    glm::vec3       velocity;
    glm::vec3       direction;
    float           pitch;
    float           yaw;
};

struct FrameState
{
    double          time;
    double          mouseX;
    double          mouseY;
};

struct AppContext
{
    GLFWwindow*     window = nullptr;
    VulkanContext*  vulkan = nullptr;
    Scene           scene;
    CameraState     camera;
    FrameState      frames[2];
    uint32_t        frameIndex = 0;

    bool            accumulateSamples;

    RenderMode      renderMode;
    uint32_t        bounceLimit = 5;
    ToneMapping     toneMapping;
    CameraType      cameraType;
    float           cameraFocalLengthInMM;
    float           cameraApertureRadiusInMM;
    float           cameraFocusDistance;

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

    // ImGui.
    bool cameraChanged = false;
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

        ImGui::Begin("Controls");

        ImGui::SeparatorText("General");
        ImGui::Checkbox("Accumulate Samples", &app.accumulateSamples);

        ImGui::SeparatorText("Render Mode");
        c |= ImGui::RadioButton("Path Tracing", (int*)&app.renderMode, RENDER_MODE_PATH_TRACE);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Albedo", (int*)&app.renderMode, RENDER_MODE_ALBEDO);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Normal", (int*)&app.renderMode, RENDER_MODE_NORMAL);
        c |= ImGui::RadioButton("Material ID", (int*)&app.renderMode, RENDER_MODE_MATERIAL_INDEX);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Primitive ID", (int*)&app.renderMode, RENDER_MODE_PRIMITIVE_INDEX);

        int bounceLimit = static_cast<int>(app.bounceLimit);
        c |= ImGui::InputInt("Bounce Limit", &bounceLimit);
        app.bounceLimit = std::max(1, bounceLimit);

        // Tone mapping operators.  Note that since tone mapping happens as
        // a post-process operation, there is no need to reset the accumulated
        // samples.
        ImGui::SeparatorText("Tone Mapping");
        ImGui::RadioButton("Clamp", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_CLAMP);
        ImGui::SameLine();
        ImGui::RadioButton("Reinhard", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_REINHARD);
        ImGui::SameLine();
        ImGui::RadioButton("Hable", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_HABLE);
        ImGui::RadioButton("ACES", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_ACES);

        if (app.toneMapping.mode == TONE_MAPPING_MODE_REINHARD) {
            ImGui::SliderFloat("White Level", &app.toneMapping.whiteLevel, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        }

        ImGui::SeparatorText("Camera");
        c |= ImGui::RadioButton("Pinhole", (int*)&app.cameraType, CAMERA_TYPE_PINHOLE);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Thin Lens", (int*)&app.cameraType, CAMERA_TYPE_THIN_LENS);
        ImGui::SameLine();
        c |= ImGui::RadioButton("360", (int*)&app.cameraType, CAMERA_TYPE_360);

        if (app.cameraType == CAMERA_TYPE_PINHOLE) {
        }

        if (app.cameraType == CAMERA_TYPE_THIN_LENS) {
            c |= ImGui::SliderFloat("Focal Length (mm)", &app.cameraFocalLengthInMM, 1.0f, 50.0f);
            c |= ImGui::SliderFloat("Aperture Radius (mm)", &app.cameraApertureRadiusInMM, 0.01f, 50.0f);
            c |= ImGui::SliderFloat("Focus Distance", &app.cameraFocusDistance, 0.01f, 1000.0f,  "%.3f", ImGuiSliderFlags_Logarithmic);
        }

        ImGui::End();
        ImGui::EndFrame();
        ImGui::Render();

        cameraChanged = c;
    }

    // Handle camera movement.
    CameraState& camera = app.camera;
    bool cameraMoved = false;
    {
        if (glfwGetMouseButton(app.window, GLFW_MOUSE_BUTTON_2)) {
            glm::vec3 delta {};
            if (glfwGetKey(app.window, GLFW_KEY_A))
                delta -= glm::cross(camera.direction, glm::vec3(0, 0, 1));
            if (glfwGetKey(app.window, GLFW_KEY_D))
                delta += glm::cross(camera.direction, glm::vec3(0, 0, 1));
            if (glfwGetKey(app.window, GLFW_KEY_W))
                delta += camera.direction;
            if (glfwGetKey(app.window, GLFW_KEY_S))
                delta -= camera.direction;
            if (glm::length(delta) > 0)
                camera.velocity = 2.0f * glm::normalize(delta);

            camera.yaw -= deltaMouseX * 0.01f;
            camera.pitch += deltaMouseY * 0.01f;
            camera.pitch = glm::clamp(camera.pitch, -glm::pi<glm::f32>() * 0.45f, +glm::pi<glm::f32>() * 0.45f);
            cameraMoved = true;
        }

        camera.direction = glm::quat(glm::vec3(0, camera.pitch, camera.yaw)) * glm::vec3(1, 0, 0);
        camera.position += deltaTime * camera.velocity;
        camera.velocity *= expf(-deltaTime / 0.05f);

        if (glm::length(camera.velocity) > 0)
            cameraMoved = true;

        if (glm::length(camera.velocity) < 1e-2f)
            camera.velocity = glm::vec3(0);
    }

    glm::mat4 viewMatrix = glm::lookAt(camera.position - camera.direction * 2.0f, camera.position, glm::vec3(0, 0, 1));

    // Render frame.
    float sensorDistance = 1.0f / (1000.0f / app.cameraFocalLengthInMM - 1.0f / app.cameraFocusDistance);

    FrameUniformBuffer uniforms = {
        .frameIndex = app.frameIndex,
        .objectCount = static_cast<uint32_t>(app.scene.objects.size()),
        .clearFrame = cameraChanged || cameraMoved || !app.accumulateSamples,
        .renderMode = app.renderMode,
        .bounceLimit = app.bounceLimit,
        .toneMapping = app.toneMapping,
        .camera = {
            .type = app.cameraType,
            .focalLength = app.cameraFocalLengthInMM / 1000.0f,
            .apertureRadius = app.cameraApertureRadiusInMM / 1000.0f,
            .sensorDistance = sensorDistance,
            .sensorSize = { 0.032f, 0.018f },
            .worldMatrix = glm::inverse(viewMatrix),
        },
    };
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
    LoadMesh(&scene, "../scene/tyra.obj", 1.0f);
    //LoadMesh(&scene, "../scene/viking_room.obj", 1.0f);
    AddTextureFromFile(&scene, "../scene/viking_room.png");
    AddTextureFromFile(&scene, "../scene/viking_room.png");
    LoadSkybox(&scene, "../scene/CloudedSunGlow4k.hdr");
    //AddMesh(&scene, glm::vec3(0, 0, 0), 0);
    //AddPlane(&scene, glm::vec3(0, 0, 0.0f));

    //scene.materials.push_back({
    //    .albedoColor = glm::vec3(1, 1, 1),
    //    .albedoTextureIndex = 0,
    //    .specularColor = glm::vec4(1, 1, 1, 0),
    //    .emissiveColor = glm::vec3(0, 0, 0),
    //    .emissiveTextureIndex = 0,
    //    .roughness = 1.0f,
    //    .specularProbability = 0.0f,
    //    .refractProbability = 0.0f,
    //    .refractIndex = 0.0f,
    //    .albedoTextureSize = glm::uvec2(1024, 1024),
    //    .padding = glm::uvec2(0, 0),
    //});

    uint32_t glassMaterialIndex = (uint32_t)scene.materials.size();
    scene.materials.push_back({
        .albedoColor = glm::vec3(1, 1, 1),
        .albedoTextureIndex = 0,
        .specularColor = glm::vec4(1, 1, 1, 0),
        .emissiveColor = glm::vec3(0, 0, 0),
        .emissiveTextureIndex = 0,
        .roughness = 0.15f,
        .specularProbability = 0.0f,
        .refractProbability = 0.0f,
        .refractIndex = 0.0f,
        .albedoTextureSize = glm::uvec2(0, 0),
        .padding = glm::uvec2(0, 0),
    });
    
    uint32_t diffuseMaterialIndex = (uint32_t)scene.materials.size();
    scene.materials.push_back({
        .albedoColor = glm::vec3(1, 1, 1),
        .albedoTextureIndex = 0,
        .specularColor = glm::vec4(1, 1, 1, 0),
        .emissiveColor = glm::vec3(0, 0, 0),
        .emissiveTextureIndex = 0,
        .roughness = 1.0f,
        .specularProbability = 0.0f,
        .refractProbability = 0.0f,
        .refractIndex = 0.0f,
        .albedoTextureSize = glm::uvec2(0, 0),
        .padding = glm::uvec2(0, 0),
    });

    uint32_t lightMaterialIndex = (uint32_t)scene.materials.size();
    scene.materials.push_back({
        .albedoColor = glm::vec3(1, 1, 1),
        .albedoTextureIndex = 0,
        .specularColor = glm::vec4(1, 1, 1, 0),
        .emissiveColor = 150.0f * glm::vec3(1,243/255.0f,142/255.0f),
        .emissiveTextureIndex = 0,
        .roughness = 1.0f,
        .specularProbability = 0.0f,
        .refractProbability = 0.0f,
        .refractIndex = 0.0f,
        .albedoTextureSize = glm::uvec2(0, 0),
        .padding = glm::uvec2(0, 0),
    });

    scene.objects.push_back({
        .origin = glm::vec3(0, 0, 0),
        .type = OBJECT_TYPE_MESH,
        .materialIndex = glassMaterialIndex,
        .meshRootNodeIndex = 0,
    });
    scene.objects.push_back({
        .origin = glm::vec3(0.5f, 0.5f, 1.75f),
        .type = OBJECT_TYPE_SPHERE,
        .scale = 0.25f * glm::vec3(1, 1, 1),
        .materialIndex = lightMaterialIndex,
    });
    scene.objects.push_back({
        .origin = glm::vec3(0, 0, -1.3),
        .type = OBJECT_TYPE_PLANE,
        .scale = glm::vec3(1, 1, 1),
        .materialIndex = diffuseMaterialIndex,
    });

    for (MeshNode const& node : scene.meshNodes) {
        if (node.faceEndIndex > 0) {
            assert(node.faceBeginOrNodeIndex <= scene.meshFaces.size());
            assert(node.faceEndIndex <= scene.meshFaces.size());
        }
        else {
            assert(node.faceBeginOrNodeIndex < scene.meshNodes.size());
        }
    }

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

    UploadScene(app.vulkan, &scene);

    app.camera.position = { 0, 0, 0 };
    app.camera.velocity = { 0, 0, 0 };
    app.camera.direction = { 0, 0, -1 };
    app.camera.pitch = 0.0f;
    app.camera.yaw = 0.0f;

    app.cameraFocusDistance = 1.0f;
    app.cameraFocalLengthInMM = 20.0f;
    app.cameraApertureRadiusInMM = 40.0f;

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
