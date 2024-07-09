#pragma once

#include "core/common.hpp"
#include "scene/scene.hpp"
#include "integrator/integrator.hpp"

struct GLFWwindow;
struct vulkan;
struct vulkan_scene;
struct vulkan_sample_buffer;
struct basic_renderer;

#include "application/preview_render.hpp"
#include "application/imgui_render.hpp"

enum selection_type
{
    SELECTION_TYPE_NONE     = 0,
    SELECTION_TYPE_TEXTURE  = 1,
    SELECTION_TYPE_MATERIAL = 2,
    SELECTION_TYPE_MESH     = 3,
    SELECTION_TYPE_PREFAB   = 4,
    SELECTION_TYPE_ENTITY   = 5,
};

struct preview_camera
{
    glm::vec3 Position;
    glm::vec3 Velocity;
    glm::vec3 Rotation;
};

struct application
{
    GLFWwindow*   Window      = nullptr;
    vulkan*       Vulkan      = nullptr;
    vulkan_scene* VulkanScene = nullptr;

    //
    preview_camera         PreviewCamera        = {};
    preview_render_mode    PreviewRenderMode    = PREVIEW_RENDER_MODE_BASE_COLOR_SHADED;
    float                  PreviewBrightness    = 1.0f;
    preview_render_context PreviewRenderContext = {};

    imgui_render_context ImGuiRenderContext = {};
    bool                 ImGuiIsVisible     = true;

    // Selection state.
    selection_type SelectionType    = SELECTION_TYPE_NONE;
    texture*       SelectedTexture  = nullptr;
    material*      SelectedMaterial = nullptr;
    mesh*          SelectedMesh     = nullptr;
    prefab*        SelectedPrefab   = nullptr;
    entity*        SelectedEntity   = nullptr;

    resolve_parameters    ResolveParameters = {};
    vulkan_sample_buffer* SampleBuffer      = nullptr;
    basic_renderer*       BasicRenderer     = nullptr;

    uint32_t FrameIndex = 0;

    scene* Scene = nullptr;
    camera_entity* SceneCameraToRender = nullptr;
};

/* --- imgui_main.cpp ------------------------------------------------------ */

void CreateImGui(application* App);
void DestroyImGui(application* App);
void ShowImGui(application* App);
