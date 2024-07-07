#pragma once

#include "core/common.h"
#include "renderer/vulkan.h"

struct basic_renderer
{
    vulkan_sample_buffer*       SampleBuffer            = nullptr;

    VkDescriptorSetLayout       DescriptorSetLayout     = {};
    VkDescriptorSet             DescriptorSet           = {};

    vulkan_buffer               PathBuffer              = {};
    vulkan_buffer               TraceBuffer             = {};

    vulkan_pipeline             ScatterPipeline         = {};
    vulkan_pipeline             TracePipeline           = {};

    uint                        FrameIndex              = 0;
    camera                      Camera                  = {};
    vulkan_scene*               Scene                   = nullptr;

    uint                        RenderFlags             = 0;
    uint                        PathLengthLimit         = 0;
    float                       PathTerminationProbability = 0.0f;
};

basic_renderer*     CreateBasicRenderer(vulkan_context* Vulkan, vulkan_sample_buffer* SampleBuffer);
void                DestroyBasicRenderer(vulkan_context* Vulkan, basic_renderer* Renderer);

void                ResetBasicRenderer(vulkan_context* Vulkan, basic_renderer* Renderer);
void                RunBasicRenderer(vulkan_context* Vulkan, basic_renderer* Renderer, uint Rounds);