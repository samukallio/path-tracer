#include <stdio.h>

#include <format>

#include "core/common.hpp"
#include "core/vulkan.hpp"
#include "scene/scene.hpp"
#include "integrator/integrator.hpp"
#include "integrator/basic.hpp"
#include "application/application.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

int const WINDOW_WIDTH = 2048;
int const WINDOW_HEIGHT = 1024;
char const* APPLICATION_NAME = "Path Tracer";

bool HandleCameraMovement(application* App)
{
    ImGuiIO& IO = ImGui::GetIO();

    bool IsEditing = !App->SceneCameraToRender;
    vec3& Position = IsEditing ? App->PreviewCamera.Position : App->SceneCameraToRender->Transform.Position;
    vec3& Velocity = IsEditing ? App->PreviewCamera.Velocity : App->SceneCameraToRender->Velocity;
    vec3& Rotation = IsEditing ? App->PreviewCamera.Rotation : App->SceneCameraToRender->Transform.Rotation;
    bool WasMoved = false;

    vec3 Forward = glm::quat(Rotation) * vec3(0, 0, -1);

    if (!IO.WantCaptureMouse && IO.MouseDown[1])
    {
        vec3 Delta {};
        if (glfwGetKey(App->Window, GLFW_KEY_A))
            Delta -= glm::cross(Forward, vec3(0, 0, 1));
        if (glfwGetKey(App->Window, GLFW_KEY_D))
            Delta += glm::cross(Forward, vec3(0, 0, 1));
        if (glfwGetKey(App->Window, GLFW_KEY_W))
            Delta += Forward;
        if (glfwGetKey(App->Window, GLFW_KEY_S))
            Delta -= Forward;
        if (glm::length(Delta) > 0)
            Velocity = 2.0f * glm::normalize(Delta);

        Rotation.z -= IO.MouseDelta.x * 0.01f;
        Rotation.z = RepeatRange(Rotation.z, -PI, +PI);
        Rotation.x -= IO.MouseDelta.y * 0.01f;
        Rotation.x = glm::clamp(Rotation.x, 0.05f * PI, +0.95f * PI);
        WasMoved = true;
    }

    Position += IO.DeltaTime * Velocity;
    Velocity *= expf(-IO.DeltaTime / 0.05f);

    if (glm::length(Velocity) > 0)
        WasMoved = true;

    if (glm::length(Velocity) < 1e-2f)
        Velocity = vec3(0);

    if (WasMoved && App->SceneCameraToRender)
    {
        App->Scene->DirtyFlags |= SCENE_DIRTY_CAMERAS;
        App->PreviewCamera.Position = App->SceneCameraToRender->Transform.Position;
        App->PreviewCamera.Rotation = App->SceneCameraToRender->Transform.Rotation;
    }

    return WasMoved;
}

void Update(application* App)
{
    ImGuiIO& IO = ImGui::GetIO();

    // ImGui.
    ImGui::NewFrame();
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
        App->ImGuiIsVisible = !App->ImGuiIsVisible;

    if (App->ImGuiIsVisible)
        ShowImGui(App);

    ImGui::EndFrame();

    // 
    bool Restart = false;

    if (HandleCameraMovement(App))
        Restart = true;

    uint DirtyFlags = PackSceneData(App->Scene);

    if (DirtyFlags != 0)
        Restart = true;

    UpdateVulkanScene(App->Vulkan, App->VulkanScene, App->Scene, DirtyFlags);

    BeginVulkanFrame(App->Vulkan);

    if (App->SceneCameraToRender)
    {
        if (Restart)
        {
            App->BasicRenderer->CameraIndex      = App->SceneCameraToRender->PackedCameraIndex;
            App->BasicRenderer->Scene            = App->VulkanScene;
            App->BasicRenderer->RenderFlags      = RENDER_FLAG_ACCUMULATE | RENDER_FLAG_SAMPLE_JITTER;
            App->BasicRenderer->PathTerminationProbability = 0.0f; //Parameters.RenderTerminationProbability;

            ResetBasicRenderer(App->Vulkan, App->BasicRenderer);
            RunBasicRenderer(App->Vulkan, App->BasicRenderer, 2);
        }
        else
        {
            RunBasicRenderer(App->Vulkan, App->BasicRenderer, 1);
        }

        auto ResolveParameters = resolve_parameters {
            .Brightness             = App->ResolveParameters.Brightness,
            .ToneMappingMode        = App->ResolveParameters.ToneMappingMode,
            .ToneMappingWhiteLevel  = App->ResolveParameters.ToneMappingWhiteLevel,
        };

        RenderSampleBuffer(App->Vulkan, App->SampleBuffer, &App->ResolveParameters);
    }
    else
    {
        preview_camera& Camera = App->PreviewCamera;

        mat4 Transform = MakeTransformMatrix(Camera.Position, Camera.Rotation);

        auto PreviewParameters = preview_parameters
        {
            .CameraTransform    = PackTransform(Transform),
            .RenderMode         = App->PreviewRenderMode,
            .Brightness         = App->PreviewBrightness,
            .SelectedShapeIndex = SHAPE_INDEX_NONE,
            .RenderSizeX        = WINDOW_WIDTH,
            .RenderSizeY        = WINDOW_HEIGHT,
            .MouseX             = static_cast<uint>(IO.MousePos.x),
            .MouseY             = static_cast<uint>(IO.MousePos.y),
        };

        if (!IO.WantCaptureMouse && IO.MouseDown[0])
        {
            preview_query_result Result;
            if (RetrievePreviewQueryResult(App->Vulkan, &App->PreviewRenderContext, &Result))
            {
                entity* Entity = FindEntityByPackedShapeIndex(App->Scene, Result.HitShapeIndex);
                if (Entity)
                {
                    App->SelectedEntity = Entity;
                    App->SelectionType = SELECTION_TYPE_ENTITY;
                }
            }
        }

        if (App->SelectionType == SELECTION_TYPE_ENTITY)
            PreviewParameters.SelectedShapeIndex = App->SelectedEntity->PackedShapeIndex;

        RenderPreview(App->Vulkan, &App->PreviewRenderContext, &PreviewParameters);
    }

    RenderImGui(App->Vulkan, &App->ImGuiRenderContext);

    EndVulkanFrame(App->Vulkan);
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
    
    ImGuiKey ImKey = [](int Key)
    {
        switch (Key)
        {
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
    } (Key);

    switch (ImKey)
    {
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

void RunApplication(application* App)
{
    scene* Scene = CreateScene();

    App->Scene = Scene;
    App->SceneCameraToRender = nullptr;

    CreateImGui(App);

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    App->Window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);
    App->Vulkan = CreateVulkan(App->Window, APPLICATION_NAME);

    App->VulkanScene = CreateVulkanScene(App->Vulkan);

    App->SampleBuffer = CreateSampleBuffer(App->Vulkan, WINDOW_WIDTH, WINDOW_HEIGHT);

    CreateImGuiRenderContext(App->Vulkan, App->VulkanScene, &App->ImGuiRenderContext);

    UpdateVulkanScene(App->Vulkan, App->VulkanScene, Scene, SCENE_DIRTY_ALL);

    CreatePreviewRenderContext(App->Vulkan, App->VulkanScene, &App->PreviewRenderContext);

    App->BasicRenderer = CreateBasicRenderer(App->Vulkan, App->VulkanScene, App->SampleBuffer);

    App->PreviewCamera.Position = { 0, 0, 0 };
    App->PreviewCamera.Velocity = { 0, 0, 0 };
    App->PreviewCamera.Rotation = { 0, 0, 0 };

    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize.x = WINDOW_WIDTH;
    IO.DisplaySize.y = WINDOW_HEIGHT;

    glfwSetMouseButtonCallback(App->Window, MouseButtonInputCallback);
    glfwSetCursorPosCallback(App->Window, MousePositionInputCallback);
    glfwSetScrollCallback(App->Window, MouseScrollInputCallback);
    glfwSetKeyCallback(App->Window, KeyInputCallback);
    glfwSetCharCallback(App->Window, CharacterInputCallback);

    double PreviousTime = glfwGetTime();
    while (!glfwWindowShouldClose(App->Window))
    {
        glfwPollEvents();

        double CurrentTime = glfwGetTime();
        IO.DeltaTime = static_cast<float>(CurrentTime - PreviousTime);
        PreviousTime = CurrentTime;

        Update(App);

        App->FrameIndex++;
    }

    vkDeviceWaitIdle(App->Vulkan->Device);

    DestroyImGuiRenderContext(App->Vulkan, &App->ImGuiRenderContext);

    DestroyPreviewRenderContext(App->Vulkan, &App->PreviewRenderContext);

    DestroyBasicRenderer(App->Vulkan, App->BasicRenderer);

    DestroySampleBuffer(App->Vulkan, App->SampleBuffer);

    DestroyVulkanScene(App->Vulkan, App->VulkanScene);

    DestroyVulkan(App->Vulkan);

    glfwDestroyWindow(App->Window);
    glfwTerminate();

    DestroyImGui(App);
}
