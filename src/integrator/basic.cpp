#include "core/common.hpp"
#include "core/vulkan.hpp"
#include "scene/scene.hpp"
#include "integrator/integrator.hpp"
#include "integrator/basic.hpp"

uint32_t const SCATTER_COMPUTE_SHADER[] =
{
    #include "basic_scatter.compute.inc"
};

uint32_t const TRACE_COMPUTE_SHADER[] =
{
    #include "basic_trace.compute.inc"
};

struct push_constant_buffer
{
    uint        CameraIndex;
    uint        RenderFlags;
    uint        PathLengthLimit;
    float       PathTerminationProbability;
    uint        RandomSeed;
    uint        Restart;
};

static void InternalDispatchTrace(
    vulkan*                 Vulkan,
    basic_renderer*         Renderer,
    push_constant_buffer*   PushConstantBuffer)
{
    auto Frame = Vulkan->CurrentFrame;

    vkCmdBindPipeline
    (
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->TracePipeline.Pipeline
    );

    VkDescriptorSet DescriptorSets[] =
    {
        Renderer->DescriptorSet,
        Renderer->Scene->DescriptorSet,
    };

    vkCmdBindDescriptorSets
    (
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->TracePipeline.PipelineLayout,
        0, 2, DescriptorSets,
        0, nullptr
    );

    vkCmdPushConstants
    (
        Frame->ComputeCommandBuffer,
        Renderer->TracePipeline.PipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push_constant_buffer), PushConstantBuffer
    );

    uint RenderSizeX = Renderer->SampleBuffer->Image.Extent.width;
    uint RenderSizeY = Renderer->SampleBuffer->Image.Extent.height;
    uint GroupCount = RenderSizeX * RenderSizeY / 256;

    vkCmdDispatch(Frame->ComputeCommandBuffer, GroupCount, 1, 1);
    
    auto TraceBufferBarrier = VkBufferMemoryBarrier
    {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = Renderer->TraceBuffer.Buffer,
        .offset              = 0,
        .size                = Renderer->TraceBuffer.Size,
    };

    vkCmdPipelineBarrier
    (
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &TraceBufferBarrier,
        0, nullptr
    );
}

static void InternalDispatchScatter
(
    vulkan*                 Vulkan,
    basic_renderer*         Renderer,
    push_constant_buffer*   PushConstantBuffer
)
{
    auto Frame = Vulkan->CurrentFrame;

    uint RandomSeed = 0;

    vkCmdBindPipeline
    (
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->ScatterPipeline.Pipeline
    );

    VkDescriptorSet DescriptorSets[] =
    {
        Renderer->DescriptorSet,
        Renderer->Scene->DescriptorSet,
    };

    vkCmdBindDescriptorSets
    (
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->ScatterPipeline.PipelineLayout,
        0, 2, DescriptorSets,
        0, nullptr
    );

    vkCmdPushConstants
    (
        Frame->ComputeCommandBuffer,
        Renderer->ScatterPipeline.PipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push_constant_buffer), PushConstantBuffer
    );

    uint RenderSizeX = Renderer->SampleBuffer->Image.Extent.width;
    uint RenderSizeY = Renderer->SampleBuffer->Image.Extent.height;
    uint GroupPixelSize = 16; //Uniforms->RenderSampleBlockSize;
    uint GroupCountX = (RenderSizeX + GroupPixelSize - 1) / GroupPixelSize;
    uint GroupCountY = (RenderSizeY + GroupPixelSize - 1) / GroupPixelSize;
    vkCmdDispatch(Frame->ComputeCommandBuffer, GroupCountX, GroupCountY, 1);

    auto TraceBufferBarrier = VkBufferMemoryBarrier
    {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = Renderer->TraceBuffer.Buffer,
        .offset              = 0,
        .size                = Renderer->TraceBuffer.Size,
    };

    vkCmdPipelineBarrier
    (
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &TraceBufferBarrier,
        0, nullptr
    );
}

basic_renderer* CreateBasicRenderer
(
    vulkan*                 Vulkan,
    vulkan_scene*           Scene,
    vulkan_sample_buffer*   SampleBuffer
)
{
    VkResult Result = VK_SUCCESS;

    auto Renderer = new basic_renderer;

    Renderer->Scene = Scene;
    Renderer->SampleBuffer = SampleBuffer;

    VkDescriptorType DescriptorTypes[] =
    {
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   // SampleAccumulatorImage
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // PathSSBO
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // TraceSSBO
    };

    Result = CreateVulkanDescriptorSetLayout(Vulkan, &Renderer->DescriptorSetLayout, DescriptorTypes);
    if (Result != VK_SUCCESS) return nullptr;

    auto TraceConfig = vulkan_compute_pipeline_configuration
    {
        .ComputeShaderCode = TRACE_COMPUTE_SHADER,
        .DescriptorSetLayouts =
        {
            Renderer->DescriptorSetLayout,
            Scene->DescriptorSetLayout,
        },
        .PushConstantBufferSize = sizeof(push_constant_buffer),
    };

    Result = CreateVulkanComputePipeline(Vulkan, &Renderer->TracePipeline, TraceConfig);
    if (Result != VK_SUCCESS) return nullptr;

    auto ScatterConfig = vulkan_compute_pipeline_configuration
    {
        .ComputeShaderCode = SCATTER_COMPUTE_SHADER,
        .DescriptorSetLayouts =
        {
            Renderer->DescriptorSetLayout,
            Scene->DescriptorSetLayout,
        },
        .PushConstantBufferSize = sizeof(push_constant_buffer),
    };

    Result = CreateVulkanComputePipeline(Vulkan, &Renderer->ScatterPipeline, ScatterConfig);
    if (Result != VK_SUCCESS) return nullptr;

    Result = CreateVulkanBuffer
    (
        Vulkan, &Renderer->TraceBuffer,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        256ull << 20
    );
    if (Result != VK_SUCCESS) return nullptr;

    Result = CreateVulkanBuffer
    (
        Vulkan, &Renderer->PathBuffer,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        144ull << 20
    );
    if (Result != VK_SUCCESS) return nullptr;

    vulkan_descriptor Descriptors[] =
    {
        {
            .Type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .Image = &SampleBuffer->Image,
        },
        {
            .Type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .Buffer = &Renderer->PathBuffer,
        },
        {
            .Type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .Buffer = &Renderer->TraceBuffer,
        },
    };

    Result = CreateVulkanDescriptorSet
    (
        Vulkan,
        Renderer->DescriptorSetLayout,
        &Renderer->DescriptorSet,
        Descriptors
    );
    if (Result != VK_SUCCESS) return nullptr;

    return Renderer;
}

void DestroyBasicRenderer
(
    vulkan*         Vulkan,
    basic_renderer* Renderer
)
{
    DestroyVulkanBuffer(Vulkan, &Renderer->PathBuffer);
    DestroyVulkanBuffer(Vulkan, &Renderer->TraceBuffer);

    DestroyVulkanPipeline(Vulkan, &Renderer->ScatterPipeline);
    DestroyVulkanPipeline(Vulkan, &Renderer->TracePipeline);

    if (Renderer->DescriptorSetLayout)
    {
        vkDestroyDescriptorSetLayout(Vulkan->Device, Renderer->DescriptorSetLayout, nullptr);
        Renderer->DescriptorSetLayout = VK_NULL_HANDLE;
    }

    delete Renderer;
}

void ResetBasicRenderer
(
    vulkan*         Vulkan,
    basic_renderer* Renderer
)
{
    auto Frame = Vulkan->CurrentFrame;

    auto PushConstantBuffer = push_constant_buffer
    {
        .CameraIndex                    = Renderer->CameraIndex,
        .RenderFlags                    = Renderer->RenderFlags,
        .PathLengthLimit                = Renderer->PathLengthLimit,
        .PathTerminationProbability     = Renderer->PathTerminationProbability,
        .RandomSeed                     = Renderer->FrameIndex,
        .Restart                        = 1u,
    };

    InternalDispatchScatter(Vulkan, Renderer, &PushConstantBuffer);
}

void RunBasicRenderer
(
    vulkan*         Vulkan,
    basic_renderer* Renderer,
    uint            Rounds
)
{
    VkResult Result = VK_SUCCESS;

    Renderer->FrameIndex += 1;

    auto PushConstantBuffer = push_constant_buffer
    {
        .CameraIndex                    = Renderer->CameraIndex,
        .RenderFlags                    = Renderer->RenderFlags,
        .PathLengthLimit                = Renderer->PathLengthLimit,
        .PathTerminationProbability     = Renderer->PathTerminationProbability,
        .RandomSeed                     = Renderer->FrameIndex,
        .Restart                        = 0u,
    };

    for (uint Round = 0; Round < Rounds; Round++)
    {
        InternalDispatchTrace(Vulkan, Renderer, &PushConstantBuffer);
        InternalDispatchScatter(Vulkan, Renderer, &PushConstantBuffer);
    }
}
