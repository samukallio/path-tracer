#pragma once

enum preview_render_mode : uint
{
    PREVIEW_RENDER_MODE_BASE_COLOR        = 0,
    PREVIEW_RENDER_MODE_BASE_COLOR_SHADED = 1,
    PREVIEW_RENDER_MODE_NORMAL            = 2,
    PREVIEW_RENDER_MODE_MATERIAL_INDEX    = 3,
    PREVIEW_RENDER_MODE_PRIMITIVE_INDEX   = 4,
    PREVIEW_RENDER_MODE_MESH_COMPLEXITY   = 5,
    PREVIEW_RENDER_MODE_SCENE_COMPLEXITY  = 6,
    PREVIEW_RENDER_MODE__COUNT            = 7,
};

struct preview_render_context
{
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet[2] = {};
    vulkan_buffer QueryBuffer[2] = {};
    vulkan_pipeline Pipeline = {};
    vulkan_scene* Scene = nullptr;
};

struct preview_parameters
{
    packed_transform CameraTransform;
    preview_render_mode RenderMode;
    float Brightness;
    uint SelectedShapeIndex;

    uint RenderSizeX;
    uint RenderSizeY;
    uint MouseX;
    uint MouseY;
};

struct preview_query_result
{
    uint HitShapeIndex;
};

inline char const* PreviewRenderModeName(preview_render_mode Mode)
{
    switch (Mode)
    {
        case PREVIEW_RENDER_MODE_BASE_COLOR:        return "Base Color";
        case PREVIEW_RENDER_MODE_BASE_COLOR_SHADED: return "Base Color (Shaded)";
        case PREVIEW_RENDER_MODE_NORMAL:            return "Normal";
        case PREVIEW_RENDER_MODE_MATERIAL_INDEX:    return "Material ID";
        case PREVIEW_RENDER_MODE_PRIMITIVE_INDEX:   return "Primitive ID";
        case PREVIEW_RENDER_MODE_MESH_COMPLEXITY:   return "Mesh Complexity";
        case PREVIEW_RENDER_MODE_SCENE_COMPLEXITY:  return "Scene Complexity";
    }
    assert(false);
    return nullptr;
}

void CreatePreviewRenderContext
(
    vulkan* Vulkan,
    vulkan_scene* Scene,
    preview_render_context* Context
);

void DestroyPreviewRenderContext
(
    vulkan* Vulkan,
    preview_render_context* Context
);

bool RetrievePreviewQueryResult
(
    vulkan* Vulkan,
    preview_render_context* Context,
    preview_query_result* Result
);

void RenderPreview
(
    vulkan* Vulkan,
    preview_render_context* Context,
    preview_parameters* Parameters
);
