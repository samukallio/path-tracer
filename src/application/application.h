#pragma once

#include "core/common.h"
#include "scene/scene.h"

struct GLFWwindow;
struct vulkan;
struct vulkan_scene;
struct vulkan_sample_buffer;
struct basic_renderer;

struct editor_camera
{
    glm::vec3       Position;
    glm::vec3       Velocity;
    glm::vec3       Rotation;
};

enum selection_type
{
    SELECTION_TYPE_NONE         = 0,
    SELECTION_TYPE_TEXTURE      = 1,
    SELECTION_TYPE_MATERIAL     = 2,
    SELECTION_TYPE_MESH         = 3,
    SELECTION_TYPE_PREFAB       = 4,
    SELECTION_TYPE_ENTITY       = 5,
};

struct application
{
    GLFWwindow*             Window              = nullptr;
    vulkan*                 Vulkan              = nullptr;
    vulkan_scene*           VulkanScene         = nullptr;

    vulkan_sample_buffer*   SampleBuffer        = nullptr;
    basic_renderer*         BasicRenderer       = nullptr;

    uint32_t                FrameIndex          = 0;

    editor_camera           EditorCamera;
    preview_render_mode     PreviewRenderMode   = PREVIEW_RENDER_MODE_BASE_COLOR_SHADED;
    float                   PreviewBrightness   = 1.0f;

    selection_type          SelectionType       = SELECTION_TYPE_NONE;
    texture*                SelectedTexture     = nullptr;
    material*               SelectedMaterial    = nullptr;
    mesh*                   SelectedMesh        = nullptr;
    prefab*                 SelectedPrefab      = nullptr;
    entity*                 SelectedEntity      = nullptr;

    scene*                  Scene               = nullptr;
    camera_entity*          Camera              = nullptr;

    bool                    ShowUI              = true;
};

void InitializeUI(application* App);
void ShowUI(application* App);
