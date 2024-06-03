#pragma once

#include "common.h"
#include "scene.h"
#include "vulkan.h"

struct GLFWwindow;

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
    SELECTION_TYPE_ENTITY       = 4,
};

struct application
{
    GLFWwindow*     Window              = nullptr;
    vulkan_context* Vulkan              = nullptr;

    uint32_t        FrameIndex          = 0;

    editor_camera   EditorCamera;

    selection_type  SelectionType       = SELECTION_TYPE_NONE;
    texture*        SelectedTexture     = nullptr;
    material*       SelectedMaterial    = nullptr;
    mesh*           SelectedMesh        = nullptr;
    entity*         SelectedEntity      = nullptr;

    scene*          Scene               = nullptr;
    camera*         Camera              = nullptr;
};

void InitializeUI(application* App);
void SceneHierarchyWindow(application* App);
void InspectorWindow(application* App);
void ResourceBrowserWindow(application* App);
