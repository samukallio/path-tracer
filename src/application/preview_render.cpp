#include "core/common.hpp"
#include "core/vulkan.hpp"
#include "scene/scene.hpp"

#include "application/preview_render.hpp"

uint32_t const PREVIEW_VERTEX_SHADER[] =
{
    #include "preview_render.vertex.inc"
};

uint32_t const PREVIEW_FRAGMENT_SHADER[] =
{
    #include "preview_render.fragment.inc"
};

void CreatePreviewRenderContext
(
    vulkan*                 Vulkan,
    vulkan_scene*           Scene,
    preview_render_context* Context
)
{
    VkResult Result = VK_SUCCESS;

    Context->Scene = Scene;

    VkDescriptorType DescriptorTypes[] =
    {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };

    CreateVulkanDescriptorSetLayout(Vulkan, &Context->DescriptorSetLayout, DescriptorTypes);

    auto PipelineConfig = vulkan_graphics_pipeline_configuration
    {
        .VertexShaderCode = PREVIEW_VERTEX_SHADER,
        .FragmentShaderCode = PREVIEW_FRAGMENT_SHADER,
        .DescriptorSetLayouts =
        {
            Scene->DescriptorSetLayout,
            Context->DescriptorSetLayout,
        },
        .PushConstantBufferSize = sizeof(preview_parameters),
    };

    Result = CreateVulkanGraphicsPipeline(Vulkan, &Context->Pipeline, PipelineConfig);
    if (Result != VK_SUCCESS) return;

    for (uint I = 0; I < 2; I++)
    {
        CreateVulkanBuffer
        (
            Vulkan,
            &Context->QueryBuffer[I],
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(preview_query_result)
        );

        vulkan_descriptor Descriptors[] =
        {
            {
                .Type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .Buffer = &Context->QueryBuffer[I],
            }
        };

        Result = CreateVulkanDescriptorSet
        (
            Vulkan,
            Context->DescriptorSetLayout,
            &Context->DescriptorSet[I],
            Descriptors
        );
        if (Result != VK_SUCCESS) return;
    }
}

void DestroyPreviewRenderContext
(
    vulkan* Vulkan,
    preview_render_context* Context
)
{
    for (uint I = 0; I < 2; I++)
    {
        DestroyVulkanDescriptorSet(Vulkan, &Context->DescriptorSet[I]);
        DestroyVulkanBuffer(Vulkan, &Context->QueryBuffer[I]);
    }

    DestroyVulkanPipeline(Vulkan, &Context->Pipeline);
    DestroyVulkanDescriptorSetLayout(Vulkan, &Context->DescriptorSetLayout);
}

bool RetrievePreviewQueryResult
(
    vulkan*                 Vulkan,
    preview_render_context* Context,
    preview_query_result*   Result
)
{
    auto Frame = Vulkan->CurrentFrame;
    assert(Frame);

    if (Frame->Fresh) return false;

    auto QueryBuffer = &Context->QueryBuffer[Frame->Index];

    void* Memory;
    vkMapMemory(Vulkan->Device, QueryBuffer->Memory, 0, sizeof(preview_query_result), 0, &Memory);
    memcpy(Result, Memory, sizeof(preview_query_result));
    vkUnmapMemory(Vulkan->Device, QueryBuffer->Memory);

    return true;
}

void RenderPreview
(
    vulkan* Vulkan,
    preview_render_context* Context,
    preview_parameters* Parameters
)
{
    auto Frame = Vulkan->CurrentFrame;
    auto Scene = Context->Scene;

    uint RandomSeed = 0;

    vkCmdBindPipeline
    (
        Frame->GraphicsCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        Context->Pipeline.Pipeline
    );

    VkDescriptorSet DescriptorSets[] =
    {
        Scene->DescriptorSet,
        Context->DescriptorSet[Frame->Index],
    };

    vkCmdBindDescriptorSets
    (
        Frame->GraphicsCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        Context->Pipeline.PipelineLayout,
        0, 2, DescriptorSets,
        0, nullptr
    );

    vkCmdPushConstants
    (
        Frame->GraphicsCommandBuffer,
        Context->Pipeline.PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(preview_parameters), Parameters
    );

    auto Viewport = VkViewport
    {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>(Vulkan->SwapChainExtent.width),
        .height   = static_cast<float>(Vulkan->SwapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    vkCmdSetViewport(Frame->GraphicsCommandBuffer, 0, 1, &Viewport);

    auto Scissor = VkRect2D
    {
        .offset = { 0, 0 },
        .extent = Vulkan->SwapChainExtent,
    };

    vkCmdSetScissor(Frame->GraphicsCommandBuffer, 0, 1, &Scissor);

    vkCmdDraw(Frame->GraphicsCommandBuffer, 6, 1, 0, 0);
}
