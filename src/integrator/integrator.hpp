#pragma once

#include "core/common.hpp"
#include "core/vulkan.hpp"

enum render_flag : uint
{
    RENDER_FLAG_ACCUMULATE    = 1 << 0,
    RENDER_FLAG_SAMPLE_JITTER = 1 << 1,
};

enum tone_mapping_mode : uint
{
    TONE_MAPPING_MODE_CLAMP    = 0,
    TONE_MAPPING_MODE_REINHARD = 1,
    TONE_MAPPING_MODE_HABLE    = 2,
    TONE_MAPPING_MODE_ACES     = 3,
    TONE_MAPPING_MODE__COUNT   = 4,
};

inline char const* ToneMappingModeName(tone_mapping_mode Mode)
{
    switch (Mode)
    {
        case TONE_MAPPING_MODE_CLAMP:    return "Clamp";
        case TONE_MAPPING_MODE_REINHARD: return "Reinhard";
        case TONE_MAPPING_MODE_HABLE:    return "Hable";
        case TONE_MAPPING_MODE_ACES:     return "ACES";
    }
    assert(false);
    return nullptr;
}

struct vulkan_sample_buffer
{
    VkDescriptorSetLayout ResolveDescriptorSetLayout = VK_NULL_HANDLE;
    vulkan_pipeline       ResolvePipeline = {};
    VkDescriptorSet       ResolveDescriptorSet = VK_NULL_HANDLE;
    vulkan_image          Image = {};
};


struct resolve_parameters
{
    float             Brightness = 1.0f;
    tone_mapping_mode ToneMappingMode = TONE_MAPPING_MODE_CLAMP;
    float             ToneMappingWhiteLevel = 1.0f;
};


vulkan_sample_buffer* CreateSampleBuffer(vulkan* Vulkan, uint Width, uint Height);

void DestroySampleBuffer(vulkan* Vulkan, vulkan_sample_buffer* SampleBuffer);

void RenderSampleBuffer
(
    vulkan* Vulkan,
    vulkan_sample_buffer* SampleBuffer,
    resolve_parameters* Parameters
);
