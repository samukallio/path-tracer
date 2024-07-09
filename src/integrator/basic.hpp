#pragma once

#include "core/common.hpp"
#include "core/vulkan.hpp"

struct basic_renderer
{
    vulkan_sample_buffer* SampleBuffer = nullptr;

    VkDescriptorSetLayout DescriptorSetLayout = {};
    VkDescriptorSet       DescriptorSet = {};

    vulkan_buffer PathBuffer = {};
    vulkan_buffer TraceBuffer = {};

    vulkan_pipeline ScatterPipeline = {};
    vulkan_pipeline TracePipeline = {};

    uint FrameIndex = 0;
    uint CameraIndex = 0;
    vulkan_scene* Scene = nullptr;

    uint RenderFlags = 0;
    uint PathLengthLimit = 0;
    float PathTerminationProbability = 0.0f;
};

basic_renderer* CreateBasicRenderer(vulkan* Vulkan, vulkan_scene* Scene, vulkan_sample_buffer* SampleBuffer);
void DestroyBasicRenderer(vulkan* Vulkan, basic_renderer* Renderer);

void ResetBasicRenderer(vulkan* Vulkan, basic_renderer* Renderer);
void RunBasicRenderer(vulkan* Vulkan, basic_renderer* Renderer, uint Rounds);
