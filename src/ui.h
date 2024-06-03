#pragma once

#include "common.h"
#include "scene.h"
#include "vulkan.h"

struct GLFWwindow;

struct EditorCamera
{
    glm::vec3       position;
    glm::vec3       velocity;
    glm::vec3       rotation;
};

enum SelectionType
{
    SELECTION_TYPE_NONE         = 0,
    SELECTION_TYPE_TEXTURE      = 1,
    SELECTION_TYPE_MATERIAL     = 2,
    SELECTION_TYPE_MESH         = 3,
    SELECTION_TYPE_ENTITY       = 4,
};

struct Application
{
    GLFWwindow*     window              = nullptr;
    VulkanContext*  vulkan              = nullptr;

    uint32_t        frameIndex          = 0;

    EditorCamera    editorCamera;

    SelectionType   selectionType       = SELECTION_TYPE_NONE;
    Texture*        selectedTexture     = nullptr;
    Material*       selectedMaterial    = nullptr;
    Mesh*           selectedMesh        = nullptr;
    Entity*         selectedEntity      = nullptr;

    Scene*          scene               = nullptr;
    Camera*         camera              = nullptr;
};

void InitializeUI(Application* app);
void SceneHierarchyWindow(Application* app);
void InspectorWindow(Application* app);
void ResourceBrowserWindow(Application* app);
