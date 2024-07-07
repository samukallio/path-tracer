#pragma once

struct scene;

#include "core/common.h"
#include "scene/scene.h"

#include <vector>
#include <span>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

struct vulkan_buffer
{
    VkBuffer                    Buffer                      = VK_NULL_HANDLE;
    VkDeviceMemory              Memory                      = VK_NULL_HANDLE;
    VkDeviceSize                Size                        = 0;
};

struct vulkan_image
{
    VkImage                     Image                       = VK_NULL_HANDLE;
    VkDeviceMemory              Memory                      = VK_NULL_HANDLE;
    VkImageView                 View                        = VK_NULL_HANDLE;
    VkImageType                 Type                        = VK_IMAGE_TYPE_1D;
    VkFormat                    Format                      = VK_FORMAT_UNDEFINED;
    VkExtent3D                  Extent                      = {};
    VkImageTiling               Tiling                      = VK_IMAGE_TILING_OPTIMAL;
    uint32_t                    LayerCount                  = 0;
};

struct vulkan_compute_pipeline_configuration
{
    using descriptor_set_layouts = std::vector<VkDescriptorSetLayout>;

    std::span<uint32_t const>   ComputeShaderCode           = {};
    descriptor_set_layouts      DescriptorSetLayouts        = {};
    uint32_t                    PushConstantBufferSize      = 0;
};

struct vulkan_pipeline
{
    VkPipeline                  Pipeline                    = VK_NULL_HANDLE;
    VkPipelineLayout            PipelineLayout              = VK_NULL_HANDLE;
};

struct vulkan_descriptor
{
    VkDescriptorType            Type                        = {};
    vulkan_buffer*              Buffer                      = nullptr;
    vulkan_image*               Image                       = nullptr;
    VkImageLayout               ImageLayout                 = VK_IMAGE_LAYOUT_GENERAL;
    VkSampler                   Sampler                     = VK_NULL_HANDLE;
};

struct vulkan_frame
{
    uint32_t                    Index                       = 0;
    bool                        Fresh                       = false;

    vulkan_frame*               Previous                    = nullptr;

    // Fence that gets signaled when the previous commands
    // accessing the resources of this frame state have been
    // completed.
    VkFence                     AvailableFence              = VK_NULL_HANDLE;

    // Swap chain image state for this frame.
    uint32_t                    ImageIndex                  = 0;
    VkSemaphore                 ImageAvailableSemaphore     = VK_NULL_HANDLE;
    VkSemaphore                 ImageFinishedSemaphore      = VK_NULL_HANDLE;

    VkSemaphore                 ComputeToComputeSemaphore   = VK_NULL_HANDLE;
    VkSemaphore                 ComputeToGraphicsSemaphore  = VK_NULL_HANDLE;
    VkCommandBuffer             GraphicsCommandBuffer       = VK_NULL_HANDLE;
    VkCommandBuffer             ComputeCommandBuffer        = VK_NULL_HANDLE;

    vulkan_buffer               ImGuiIndexBuffer            = {};
    vulkan_buffer               ImGuiVertexBuffer           = {};
    VkDescriptorSet             ImGuiDescriptorSet          = {};
};

struct vulkan_sample_buffer
{
    VkDescriptorSet             ResolveDescriptorSet        = VK_NULL_HANDLE;
    vulkan_image                Image                       = {};
};

struct vulkan_context
{
    VkInstance                  Instance                    = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT    Messenger                   = VK_NULL_HANDLE;

    VkPhysicalDevice            PhysicalDevice              = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures    PhysicalDeviceFeatures      = {};
    VkPhysicalDeviceProperties  PhysicalDeviceProperties    = {};

    VkDevice                    Device                      = VK_NULL_HANDLE;

    uint32_t                    GraphicsQueueFamilyIndex    = 0;
    VkQueue                     GraphicsQueue               = VK_NULL_HANDLE;
    VkCommandPool               GraphicsCommandPool         = VK_NULL_HANDLE;

    uint32_t                    ComputeQueueFamilyIndex     = 0;
    VkQueue                     ComputeQueue                = VK_NULL_HANDLE;
    VkCommandPool               ComputeCommandPool          = VK_NULL_HANDLE;

    uint32_t                    PresentQueueFamilyIndex     = 0;
    VkQueue                     PresentQueue                = VK_NULL_HANDLE;
    VkPresentModeKHR            PresentMode                 = VK_PRESENT_MODE_IMMEDIATE_KHR;

    GLFWwindow*                 Window                      = nullptr;
    VkSurfaceKHR                Surface                     = VK_NULL_HANDLE;
    VkSurfaceFormatKHR          SurfaceFormat               = {};

    VkSwapchainKHR              SwapChain                   = VK_NULL_HANDLE;
    VkExtent2D                  SwapChainExtent             = {};
    VkFormat                    SwapChainFormat             = VK_FORMAT_UNDEFINED;
    std::vector<VkImage>        SwapChainImages             = {};
    std::vector<VkImageView>    SwapChainImageViews         = {};
    std::vector<VkFramebuffer>  SwapChainFrameBuffers       = {};

    VkDescriptorPool            DescriptorPool              = VK_NULL_HANDLE;

    VkRenderPass                MainRenderPass              = VK_NULL_HANDLE;

    uint32_t                    FrameIndex                  = 0;
    vulkan_frame                FrameStates[2]              = {};

    VkSampler                   ImageSamplerNearestNoMip    = VK_NULL_HANDLE;
    VkSampler                   ImageSamplerLinear          = VK_NULL_HANDLE;
    VkSampler                   ImageSamplerLinearNoMip     = VK_NULL_HANDLE;

    VkDescriptorSetLayout       SceneDescriptorSetLayout    = VK_NULL_HANDLE;

    vulkan_pipeline             PreviewPipeline             = {};

    VkDescriptorSetLayout       ResolveDescriptorSetLayout  = VK_NULL_HANDLE;
    vulkan_pipeline             ResolvePipeline             = {};

    VkDescriptorSetLayout       ImGuiDescriptorSetLayout    = VK_NULL_HANDLE;
    vulkan_pipeline             ImGuiPipeline               = {};
    vulkan_image                ImGuiTexture                = {};

    std::vector<vulkan_sample_buffer*> SampleBuffers        = {};

    vulkan_frame*               CurrentFrame                = nullptr;
};

struct vulkan_scene
{
    VkDescriptorSet             DescriptorSet               = VK_NULL_HANDLE;
    vulkan_buffer               UniformBuffer               = {};
    vulkan_image                ImageArray                  = {};
    vulkan_buffer               TextureBuffer               = {};
    vulkan_buffer               MaterialBuffer              = {};
    vulkan_buffer               ShapeBuffer                 = {};
    vulkan_buffer               ShapeNodeBuffer             = {};
    vulkan_buffer               MeshFaceBuffer              = {};
    vulkan_buffer               MeshVertexBuffer            = {};
    vulkan_buffer               MeshNodeBuffer              = {};
};

struct vulkan_render_sample_buffer_parameters
{
    float                       Brightness                      = 1.0f;
    tone_mapping_mode           ToneMappingMode                 = TONE_MAPPING_MODE_CLAMP;
    float                       ToneMappingWhiteLevel           = 1.0f;
};

vulkan_context* CreateVulkan(GLFWwindow* Window, char const* ApplicationName);
void            DestroyVulkan(vulkan_context* Vulkan);

VkResult        CreateBuffer(vulkan_context* Vulkan, vulkan_buffer* Buffer, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryFlags, VkDeviceSize Size);
void            DestroyBuffer(vulkan_context* Vulkan, vulkan_buffer* Buffer);
void            WriteToHostVisibleBuffer(vulkan_context* Vulkan, vulkan_buffer* Buffer, void const* Data, size_t Size);

VkResult        CreateDescriptorSetLayout(vulkan_context* Vulkan, VkDescriptorSetLayout* Layout, std::span<VkDescriptorType> DescriptorTypes);
void            WriteDescriptorSet(vulkan_context* Vulkan, VkDescriptorSet DescriptorSet, std::span<vulkan_descriptor> Descriptors);
VkResult        CreateDescriptorSet(vulkan_context* Vulkan, VkDescriptorSetLayout DescriptorSetLayout, VkDescriptorSet* DescriptorSet, std::span<vulkan_descriptor> Descriptors);

VkResult        CreateComputePipeline(vulkan_context* Vulkan, vulkan_pipeline* Pipeline, vulkan_compute_pipeline_configuration const& Config);
void            DestroyPipeline(vulkan_context* Vulkan, vulkan_pipeline* Pipeline);

vulkan_scene*   CreateVulkanScene(vulkan_context* Vulkan);
void            UpdateVulkanScene(vulkan_context* Vulkan, vulkan_scene* VulkanScene, scene* Scene, uint32_t Flags);
void            DestroyVulkanScene(vulkan_context* Vulkan, vulkan_scene* VulkanScene);

vulkan_sample_buffer* CreateSampleBuffer(vulkan_context* Vulkan, uint Width, uint Height);
void            DestroySampleBuffer(vulkan_context* Vulkan, vulkan_sample_buffer* SampleBuffer);

VkResult        BeginFrame(vulkan_context* Vulkan);
void            RenderSampleBuffer(vulkan_context* Vulkan, vulkan_sample_buffer* SampleBuffer, vulkan_render_sample_buffer_parameters* Parameters);
void            RenderPreview(vulkan_context* Vulkan, vulkan_scene* Scene, camera const& Camera, render_mode RenderMode);
void            RenderImGui(vulkan_context* Vulkan, vulkan_scene* Scene, ImDrawData* DrawData);
VkResult        EndFrame(vulkan_context* Vulkan);
