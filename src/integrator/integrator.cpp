#include "core/common.hpp"
#include "core/vulkan.hpp"
#include "integrator/integrator.hpp"

uint32_t const RESOLVE_VERTEX_SHADER[] =
{
    #include "resolve.vertex.inc"
};

uint32_t const RESOLVE_FRAGMENT_SHADER[] =
{
    #include "resolve.fragment.inc"
};

vulkan_sample_buffer* CreateSampleBuffer
(
    vulkan* Vulkan,
    uint    Width,
    uint    Height
)
{
    VkResult Result = VK_SUCCESS;

    auto SampleBuffer = new vulkan_sample_buffer;

    VkDescriptorType ResolveDescriptorTypes[] =
    {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // SampleAccumulatorImage
    };

    Result = CreateVulkanDescriptorSetLayout(Vulkan, &SampleBuffer->ResolveDescriptorSetLayout, ResolveDescriptorTypes);
    if (Result != VK_SUCCESS) return nullptr;

    auto ResolveConfig = vulkan_graphics_pipeline_configuration
    {
        .VertexSize             = 0,
        .VertexFormat           = {},
        .VertexShaderCode       = RESOLVE_VERTEX_SHADER,
        .FragmentShaderCode     = RESOLVE_FRAGMENT_SHADER,
        .DescriptorSetLayouts   = { SampleBuffer->ResolveDescriptorSetLayout },
        .PushConstantBufferSize = sizeof(resolve_parameters),
    };

    Result = CreateVulkanGraphicsPipeline(Vulkan, &SampleBuffer->ResolvePipeline, ResolveConfig);
    if (Result != VK_SUCCESS) return nullptr;

    Result = CreateVulkanImage
    (
        Vulkan,
        &SampleBuffer->Image,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        { .width = Width, .height = Height, .depth = 1 },
        0,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        true
    );

    if (Result != VK_SUCCESS) return nullptr;
            
    vulkan_descriptor ResolveDescriptors[] =
    {
        {
            .Type        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .Image       = &SampleBuffer->Image,
            .ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .Sampler     = Vulkan->ImageSamplerLinear
        }
    };

    Result = CreateVulkanDescriptorSet
    (
        Vulkan,
        SampleBuffer->ResolveDescriptorSetLayout,
        &SampleBuffer->ResolveDescriptorSet,
        ResolveDescriptors
    );

    if (Result != VK_SUCCESS) return nullptr;

    Vulkan->SharedImages.push_back(SampleBuffer->Image.Image);

    return SampleBuffer;
}

void DestroySampleBuffer
(
    vulkan*                 Vulkan,
    vulkan_sample_buffer*   SampleBuffer
)
{
    std::erase(Vulkan->SharedImages, SampleBuffer->Image.Image);

    DestroyVulkanDescriptorSet(Vulkan, &SampleBuffer->ResolveDescriptorSet);
    DestroyVulkanImage(Vulkan, &SampleBuffer->Image);
    DestroyVulkanPipeline(Vulkan, &SampleBuffer->ResolvePipeline);
    DestroyVulkanDescriptorSetLayout(Vulkan, &SampleBuffer->ResolveDescriptorSetLayout);

    delete SampleBuffer;
}

void RenderSampleBuffer
(
    vulkan*                 Vulkan,
    vulkan_sample_buffer*   SampleBuffer,
    resolve_parameters*     Parameters
)
{
    auto Frame = Vulkan->CurrentFrame;

    auto Viewport = VkViewport
    {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>(Vulkan->SwapChainExtent.width),
        .height   = static_cast<float>(Vulkan->SwapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    vkCmdBindPipeline
    (
        Frame->GraphicsCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        SampleBuffer->ResolvePipeline.Pipeline
    );

    vkCmdBindDescriptorSets
    (
        Frame->GraphicsCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        SampleBuffer->ResolvePipeline.PipelineLayout,
        0, 1, &SampleBuffer->ResolveDescriptorSet,
        0, nullptr
    );

    vkCmdSetViewport(Frame->GraphicsCommandBuffer, 0, 1, &Viewport);

    auto Scissor = VkRect2D
    {
        .offset = { 0, 0 },
        .extent = Vulkan->SwapChainExtent,
    };

    vkCmdSetScissor(Frame->GraphicsCommandBuffer, 0, 1, &Scissor);

    vkCmdPushConstants
    (
        Frame->GraphicsCommandBuffer,
        SampleBuffer->ResolvePipeline.PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(resolve_parameters), Parameters
    );

    vkCmdDraw(Frame->GraphicsCommandBuffer, 6, 1, 0, 0);
}
