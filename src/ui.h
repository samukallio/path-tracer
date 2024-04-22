#pragma once

#include "common.h"
#include "scene.h"

enum SelectionType
{
    SELECTION_TYPE_NONE         = 0,
    SELECTION_TYPE_TEXTURE      = 1,
    SELECTION_TYPE_MATERIAL     = 2,
    SELECTION_TYPE_MESH         = 3,
    SELECTION_TYPE_ENTITY       = 4,
};

struct UIContext
{
    SelectionType   selectionType   = SELECTION_TYPE_NONE;
    Texture*        texture         = nullptr;
    Material*       material        = nullptr;
    Mesh*           mesh            = nullptr;
    Entity*         entity          = nullptr;

    Scene*          scene           = nullptr;
    Camera*         camera          = nullptr;
};

void InitializeImGui();
void ShowSceneHierarchyWindow(UIContext* context);
void ShowInspectorWindow(UIContext* context);
void ShowResourcesWindow(UIContext* context);
