#include "core/common.h"
#include "renderer/vulkan.h"
#include "renderer/basic.h"

uint32_t const RENDER_WIDTH = 2048;
uint32_t const RENDER_HEIGHT = 1024;

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
    camera      Camera;
    uint        RenderFlags;
    uint        PathLengthLimit;
    float       PathTerminationProbability;
    uint        RandomSeed;
    uint        Restart;
};

static void InternalDispatchTrace(
    vulkan_context*         Vulkan,
    vulkan_frame*           Frame,
    basic_renderer*         Renderer,
    push_constant_buffer*   PushConstantBuffer)
{
    vkCmdBindPipeline(
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->TracePipeline.Pipeline);

    VkDescriptorSet DescriptorSets[] = {
        Renderer->DescriptorSet,
        Renderer->Scene->DescriptorSet,
    };

    vkCmdBindDescriptorSets(
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->TracePipeline.PipelineLayout,
        0, 2, DescriptorSets,
        0, nullptr);

    vkCmdPushConstants(
        Frame->ComputeCommandBuffer,
        Renderer->TracePipeline.PipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push_constant_buffer), PushConstantBuffer);

    vkCmdDispatch(Frame->ComputeCommandBuffer, 2048*1024 / 256, 1, 1);

    auto TraceBufferBarrier = VkBufferMemoryBarrier {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = Renderer->TraceBuffer.Buffer,
        .offset              = 0,
        .size                = Renderer->TraceBuffer.Size,
    };

    vkCmdPipelineBarrier(Frame->ComputeCommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &TraceBufferBarrier,
        0, nullptr);
}

static void InternalDispatchScatter(
    vulkan_context*         Vulkan,
    vulkan_frame*           Frame,
    basic_renderer*         Renderer,
    push_constant_buffer*   PushConstantBuffer)
{
    uint RandomSeed = 0;

    vkCmdBindPipeline(
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->ScatterPipeline.Pipeline);

    VkDescriptorSet DescriptorSets[] = {
        Renderer->DescriptorSet,
        Renderer->Scene->DescriptorSet,
    };

    vkCmdBindDescriptorSets(
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Renderer->ScatterPipeline.PipelineLayout,
        0, 2, DescriptorSets,
        0, nullptr);

    vkCmdPushConstants(
        Frame->ComputeCommandBuffer,
        Renderer->ScatterPipeline.PipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push_constant_buffer), PushConstantBuffer);

    uint32_t GroupPixelSize = 16; //Uniforms->RenderSampleBlockSize;
    uint32_t GroupCountX = (RENDER_WIDTH + GroupPixelSize - 1) / GroupPixelSize;
    uint32_t GroupCountY = (RENDER_HEIGHT + GroupPixelSize - 1) / GroupPixelSize;
    vkCmdDispatch(Frame->ComputeCommandBuffer, GroupCountX, GroupCountY, 1);

    auto TraceBufferBarrier = VkBufferMemoryBarrier {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = Renderer->TraceBuffer.Buffer,
        .offset              = 0,
        .size                = Renderer->TraceBuffer.Size,
    };

    vkCmdPipelineBarrier(Frame->ComputeCommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &TraceBufferBarrier,
        0, nullptr);
}

basic_renderer* CreateBasicRenderer(
    vulkan_context*         Vulkan,
    vulkan_sample_buffer*   SampleBuffer)
{
    VkResult Result = VK_SUCCESS;

    auto Renderer = new basic_renderer;

    Renderer->SampleBuffer = SampleBuffer;

    VkDescriptorType DescriptorTypes[] = {
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   // SampleAccumulatorImage
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // PathSSBO
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // TraceSSBO
    };

    Result = CreateDescriptorSetLayout(Vulkan, &Renderer->DescriptorSetLayout, DescriptorTypes);
    if (Result != VK_SUCCESS) return nullptr;

    auto TraceConfig = vulkan_compute_pipeline_configuration {
        .ComputeShaderCode = TRACE_COMPUTE_SHADER,
        .DescriptorSetLayouts = {
            Renderer->DescriptorSetLayout,
            Vulkan->SceneDescriptorSetLayout,
        },
        .PushConstantBufferSize = sizeof(push_constant_buffer),
    };

    Result = CreateComputePipeline(Vulkan, &Renderer->TracePipeline, TraceConfig);
    if (Result != VK_SUCCESS) return nullptr;

    auto ScatterConfig = vulkan_compute_pipeline_configuration {
        .ComputeShaderCode = SCATTER_COMPUTE_SHADER,
        .DescriptorSetLayouts = {
            Renderer->DescriptorSetLayout,
            Vulkan->SceneDescriptorSetLayout,
        },
        .PushConstantBufferSize = sizeof(push_constant_buffer),
    };

    Result = CreateComputePipeline(Vulkan, &Renderer->ScatterPipeline, ScatterConfig);
    if (Result != VK_SUCCESS) return nullptr;

    Result = CreateBuffer(Vulkan,
        &Renderer->TraceBuffer,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        256ull << 20);
    if (Result != VK_SUCCESS) return nullptr;

    Result = CreateBuffer(Vulkan,
        &Renderer->PathBuffer,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        144ull << 20);
    if (Result != VK_SUCCESS) return nullptr;

    vulkan_descriptor Descriptors[] = {
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

    Result = CreateDescriptorSet(Vulkan,
        Renderer->DescriptorSetLayout,
        &Renderer->DescriptorSet,
        Descriptors);
    if (Result != VK_SUCCESS) return nullptr;

    return Renderer;
}

void DestroyBasicRenderer(
    vulkan_context*     Vulkan,
    basic_renderer*     Renderer)
{
    DestroyBuffer(Vulkan, &Renderer->PathBuffer);
    DestroyBuffer(Vulkan, &Renderer->TraceBuffer);

    DestroyPipeline(Vulkan, &Renderer->ScatterPipeline);
    DestroyPipeline(Vulkan, &Renderer->TracePipeline);

    if (Renderer->DescriptorSetLayout) {
        vkDestroyDescriptorSetLayout(Vulkan->Device, Renderer->DescriptorSetLayout, nullptr);
        Renderer->DescriptorSetLayout = VK_NULL_HANDLE;
    }

    delete Renderer;
}

void ResetBasicRenderer(
    vulkan_context*     Vulkan,
    vulkan_frame*       Frame,
    basic_renderer*     Renderer)
{
    auto PushConstantBuffer = push_constant_buffer {
        .Camera                         = Renderer->Camera,
        .RenderFlags                    = Renderer->RenderFlags,
        .PathLengthLimit                = Renderer->PathLengthLimit,
        .PathTerminationProbability     = Renderer->PathTerminationProbability,
        .RandomSeed                     = Renderer->FrameIndex,
        .Restart                        = 1u,
    };

    InternalDispatchScatter(Vulkan, Frame, Renderer, &PushConstantBuffer);
}

void RunBasicRenderer(
    vulkan_context*     Vulkan,
    vulkan_frame*       Frame,
    basic_renderer*     Renderer,
    uint                Rounds)
{
    VkResult Result = VK_SUCCESS;

    Renderer->FrameIndex += 1;

    auto PushConstantBuffer = push_constant_buffer {
        .Camera                         = Renderer->Camera,
        .RenderFlags                    = Renderer->RenderFlags,
        .PathLengthLimit                = Renderer->PathLengthLimit,
        .PathTerminationProbability     = Renderer->PathTerminationProbability,
        .RandomSeed                     = Renderer->FrameIndex,
        .Restart                        = 0u,
    };

    for (uint Round = 0; Round < Rounds; Round++) {
        InternalDispatchTrace(Vulkan, Frame, Renderer, &PushConstantBuffer);
        InternalDispatchScatter(Vulkan, Frame, Renderer, &PushConstantBuffer);
    }
}
