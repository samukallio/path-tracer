#pragma once

struct imgui_render_context
{
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    vulkan_pipeline Pipeline = {};

    vulkan_image Texture = {};
    vulkan_buffer IndexBuffer[2] = {};
    vulkan_buffer VertexBuffer[2] = {};
    VkDescriptorSet DescriptorSet[2] = {};

    vulkan_scene* Scene = nullptr;
};

void CreateImGuiRenderContext
(
    vulkan* Vulkan,
    vulkan_scene* Scene,
    imgui_render_context* Context
);

void DestroyImGuiRenderContext
(
    vulkan* Vulkan,
    imgui_render_context* Context
);

void RenderImGui
(
    vulkan* Vulkan,
    imgui_render_context* Context
);
