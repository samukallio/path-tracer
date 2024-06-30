#include <stdio.h>

#include <format>

#include "path-tracer.h"
#include "application/application.h"
#include "renderer/vulkan.h"
#include "scene/scene.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

int const WINDOW_WIDTH = 2048;
int const WINDOW_HEIGHT = 1024;
char const* APPLICATION_NAME = "Path Tracer";

application App;

void Frame()
{
    ImGuiIO& IO = ImGui::GetIO();

    // ImGui.
    ImGui::NewFrame();
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
        App.ShowUI = !App.ShowUI;
    }
    if (App.ShowUI) {
        MainMenuBar(&App);
        ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::ShowDemoWindow();
        InspectorWindow(&App);
        TextureBrowserWindow(&App);
        MaterialBrowserWindow(&App);
        MeshBrowserWindow(&App);
        PrefabBrowserWindow(&App);
        SceneHierarchyWindow(&App);
        ParametricSpectrumViewerWindow(&App);
    }
    ImGui::EndFrame();
    ImGui::Render();

    frame_uniform_buffer Uniforms = {};

    bool Restart = false;

    // Handle camera movement.
    {
        bool IsEditing = !App.Camera;
        vec3& Position = IsEditing ? App.EditorCamera.Position : App.Camera->Transform.Position;
        vec3& Velocity = IsEditing ? App.EditorCamera.Velocity : App.Camera->Velocity;
        vec3& Rotation = IsEditing ? App.EditorCamera.Rotation : App.Camera->Transform.Rotation;
        bool WasMoved = false;

        vec3 Forward = glm::quat(Rotation) * vec3(1, 0, 0);

        if (!IO.WantCaptureMouse && IO.MouseDown[1]) {
            vec3 Delta {};
            if (glfwGetKey(App.Window, GLFW_KEY_A))
                Delta -= glm::cross(Forward, vec3(0, 0, 1));
            if (glfwGetKey(App.Window, GLFW_KEY_D))
                Delta += glm::cross(Forward, vec3(0, 0, 1));
            if (glfwGetKey(App.Window, GLFW_KEY_W))
                Delta += Forward;
            if (glfwGetKey(App.Window, GLFW_KEY_S))
                Delta -= Forward;
            if (glm::length(Delta) > 0)
                Velocity = 2.0f * glm::normalize(Delta);

            Rotation.z -= IO.MouseDelta.x * 0.01f;
            Rotation.z = RepeatRange(Rotation.z, -PI, +PI);
            Rotation.y += IO.MouseDelta.y * 0.01f;
            Rotation.y = glm::clamp(Rotation.y, -0.45f * PI, +0.45f * PI);
            WasMoved = true;
        }

        Position += IO.DeltaTime * Velocity;
        Velocity *= expf(-IO.DeltaTime / 0.05f);

        if (glm::length(Velocity) > 0)
            WasMoved = true;

        if (glm::length(Velocity) < 1e-2f)
            Velocity = vec3(0);

        if (WasMoved && App.Camera) {
            App.Scene->DirtyFlags |= SCENE_DIRTY_CAMERAS;
            App.EditorCamera.Position = App.Camera->Transform.Position;
            App.EditorCamera.Rotation = App.Camera->Transform.Rotation;
        }

        if (WasMoved) {
            Restart = true;
        }
    }

    if (!App.Camera) {
        editor_camera& Camera = App.EditorCamera;

        vec3 Forward = glm::quat(Camera.Rotation) * vec3(1, 0, 0);
        mat4 ViewMatrix = glm::lookAt(Camera.Position - Forward * 2.0f, Camera.Position, vec3(0, 0, 1));
        mat4 WorldMatrix = glm::inverse(ViewMatrix);

        if (!IO.WantCaptureMouse && IO.MouseDown[0]) {
            vec2 SensorSize = { 0.032f, 0.018f };

            vec2 SamplePositionNormalized = {
                IO.MousePos.x / WINDOW_WIDTH,
                IO.MousePos.y / WINDOW_HEIGHT
            };

            vec3 SensorPositionNormalized = {
                -SensorSize.x * (SamplePositionNormalized.x - 0.5),
                -SensorSize.y * (0.5 - SamplePositionNormalized.y),
                0.020f
            };

            vec3 RayVector = -SensorPositionNormalized;

            ray Ray;
            Ray.Origin = (WorldMatrix * vec4(0, 0, 0, 1)).xyz;
            Ray.Vector = glm::normalize(WorldMatrix * vec4(RayVector, 0)).xyz;

            hit Hit;
            if (Trace(App.Scene, Ray, Hit)) {
                entity* Entity = FindEntityByPackedShapeIndex(App.Scene, Hit.ShapeIndex);
                if (Entity) {
                    App.SelectedEntity = Entity;
                    App.SelectionType = SELECTION_TYPE_ENTITY;
                    Restart = true;
                }
            }
        }

        Uniforms.RenderMode = RENDER_MODE_BASE_COLOR_SHADED;
        Uniforms.CameraModel = CAMERA_MODEL_PINHOLE;
        Uniforms.CameraSensorDistance = 0.020f;
        Uniforms.CameraSensorSize = { 0.032f, 0.018f };
        Uniforms.CameraApertureRadius = 0.0f;
        Uniforms.CameraTransform = {
            .To = WorldMatrix,
            .From = ViewMatrix,
        };
        Uniforms.RenderFlags |= RENDER_FLAG_ACCUMULATE;
        Uniforms.RenderFlags |= RENDER_FLAG_SAMPLE_JITTER;

        Uniforms.RenderBounceLimit = 0;
        Uniforms.Brightness = 1.0f;
        Uniforms.ToneMappingMode = TONE_MAPPING_MODE_CLAMP;
        Uniforms.ToneMappingWhiteLevel = 1.0f;

        if (App.SelectionType == SELECTION_TYPE_ENTITY)
            Uniforms.SelectedShapeIndex = App.SelectedEntity->PackedShapeIndex;
        else
            Uniforms.SelectedShapeIndex = SHAPE_INDEX_NONE;
    }
    else {
        camera* Camera = App.Camera;

        vec3 Origin = Camera->Transform.Position;
        vec3 Forward = glm::quat(Camera->Transform.Rotation) * vec3(1, 0, 0);
        mat4 ViewMatrix = glm::lookAt(Origin - Forward * 2.0f, Origin, vec3(0, 0, 1));
        mat4 WorldMatrix = glm::inverse(ViewMatrix);

        Uniforms.RenderMode = Camera->RenderMode;
        Uniforms.CameraModel = Camera->CameraModel;

        if (Camera->CameraModel == CAMERA_MODEL_PINHOLE) {
            float const ASPECT_RATIO = WINDOW_WIDTH / float(WINDOW_HEIGHT);
            Uniforms.CameraApertureRadius = Camera->Pinhole.ApertureDiameterInMM / 2000.0f;
            Uniforms.CameraSensorSize.x   = 2 * glm::tan(glm::radians(Camera->Pinhole.FieldOfViewInDegrees / 2));
            Uniforms.CameraSensorSize.y   = Uniforms.CameraSensorSize.x / ASPECT_RATIO;
            Uniforms.CameraSensorDistance = 1.0f;
        }

        if (Camera->CameraModel == CAMERA_MODEL_THIN_LENS) {
            Uniforms.CameraFocalLength    = Camera->ThinLens.FocalLengthInMM / 1000.0f;
            Uniforms.CameraApertureRadius = Camera->ThinLens.ApertureDiameterInMM / 2000.0f;
            Uniforms.CameraSensorDistance = 1.0f / (1000.0f / Camera->ThinLens.FocalLengthInMM - 1.0f / Camera->ThinLens.FocusDistance);
            Uniforms.CameraSensorSize     = Camera->ThinLens.SensorSizeInMM / 1000.0f;
        }

        Uniforms.CameraTransform = {
            .To = WorldMatrix,
            .From = ViewMatrix,
        };
        Uniforms.SelectedShapeIndex           = SHAPE_INDEX_NONE;
        Uniforms.RenderFlags                  = RENDER_FLAG_SAMPLE_JITTER;
        Uniforms.RenderSampleBlockSize        = 1u << Camera->RenderSampleBlockSizeLog2;
        Uniforms.RenderBounceLimit            = Camera->RenderBounceLimit;
        Uniforms.RenderTerminationProbability = Camera->RenderTerminationProbability;
        Uniforms.RenderMeshComplexityScale    = Camera->RenderMeshComplexityScale;
        Uniforms.RenderSceneComplexityScale   = Camera->RenderSceneComplexityScale;
        Uniforms.Brightness                   = Camera->Brightness;
        Uniforms.ToneMappingMode              = Camera->ToneMappingMode;
        Uniforms.ToneMappingWhiteLevel        = Camera->ToneMappingWhiteLevel;
        Uniforms.RenderFlags                  = Camera->RenderFlags;
    }

    if (App.Scene->DirtyFlags != 0)
        Restart = true;

    uint DirtyFlags = PackSceneData(App.Scene);
    UploadScene(App.Vulkan, App.Scene, DirtyFlags);

    RenderFrame(App.Vulkan, &Uniforms, Restart, ImGui::GetDrawData());
}

static void MouseButtonInputCallback(GLFWwindow* Window, int Button, int Action, int Mods)
{
    ImGuiIO& IO = ImGui::GetIO();
    IO.AddMouseButtonEvent(Button, Action == GLFW_PRESS);
}

static void MousePositionInputCallback(GLFWwindow* Window, double X, double Y)
{
    ImGuiIO& IO = ImGui::GetIO();
    IO.AddMousePosEvent(static_cast<float>(X), static_cast<float>(Y));
}

static void MouseScrollInputCallback(GLFWwindow* Window, double X, double Y)
{
    ImGuiIO& IO = ImGui::GetIO();
    IO.AddMouseWheelEvent(static_cast<float>(X), static_cast<float>(Y));
}

static void KeyInputCallback(GLFWwindow* Window, int Key, int ScanCode, int Action, int Mods)
{
    if (Action != GLFW_PRESS && Action != GLFW_RELEASE)
        return;

    ImGuiIO& IO = ImGui::GetIO();
    
    ImGuiKey ImKey = [](int Key) {
        switch (Key) {
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
    }(Key);

    switch (ImKey) {
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
        IO.AddKeyEvent(ImGuiMod_Shift, Action == GLFW_PRESS);
        break;

    default:
        IO.AddKeyEvent(ImKey, Action == GLFW_PRESS);
        break;
    }
}

static void CharacterInputCallback(GLFWwindow* Window, unsigned int CodePoint)
{
    ImGuiIO& IO = ImGui::GetIO();
    IO.AddInputCharacter(CodePoint);
}

int main()
{
    scene* Scene = CreateScene();

    App.Scene = Scene;
    App.Camera = nullptr;

    InitializeUI(&App);

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    App.Window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);
    App.Vulkan = CreateVulkan(App.Window, APPLICATION_NAME);

    App.EditorCamera.Position = { 0, 0, 0 };
    App.EditorCamera.Velocity = { 0, 0, 0 };
    App.EditorCamera.Rotation = { 0, 0, 0 };

    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize.x = WINDOW_WIDTH;
    IO.DisplaySize.y = WINDOW_HEIGHT;

    glfwSetMouseButtonCallback(App.Window, MouseButtonInputCallback);
    glfwSetCursorPosCallback(App.Window, MousePositionInputCallback);
    glfwSetScrollCallback(App.Window, MouseScrollInputCallback);
    glfwSetKeyCallback(App.Window, KeyInputCallback);
    glfwSetCharCallback(App.Window, CharacterInputCallback);

    double PreviousTime = glfwGetTime();
    while (!glfwWindowShouldClose(App.Window)) {
        glfwPollEvents();

        double CurrentTime = glfwGetTime();
        IO.DeltaTime = static_cast<float>(CurrentTime - PreviousTime);
        PreviousTime = CurrentTime;

        Frame();

        App.FrameIndex++;
    }

    DestroyVulkan(App.Vulkan);

    glfwDestroyWindow(App.Window);
    glfwTerminate();

    ImGui::DestroyContext();
}
