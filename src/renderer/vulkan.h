#pragma once

struct scene;

#include "common.h"

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

struct vulkan_pipeline
{
    VkPipeline                  Pipeline                    = VK_NULL_HANDLE;
    VkPipelineLayout            PipelineLayout              = VK_NULL_HANDLE;
};

struct vulkan_frame_state
{
    uint32_t                    Index                       = 0;
    bool                        Fresh                       = false;

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

    vulkan_buffer               FrameUniformBuffer          = {};
    VkDescriptorSet             ComputeDescriptorSet        = VK_NULL_HANDLE;

    VkDescriptorSet             ResolveDescriptorSet        = VK_NULL_HANDLE;

    vulkan_buffer               ImGuiIndexBuffer            = {};
    vulkan_buffer               ImGuiVertexBuffer           = {};
    VkDescriptorSet             ImGuiDescriptorSet          = {};
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
    vulkan_frame_state          FrameStates[2]              = {};

    VkSampler                   ImageSamplerNearestNoMip    = VK_NULL_HANDLE;
    VkSampler                   ImageSamplerLinear          = VK_NULL_HANDLE;
    VkSampler                   ImageSamplerLinearNoMip     = VK_NULL_HANDLE;

    VkDescriptorSetLayout       SceneDescriptorSetLayout    = VK_NULL_HANDLE;
    VkDescriptorSet             SceneDescriptorSet          = VK_NULL_HANDLE;
    vulkan_buffer               SceneUniformBuffer          = {};
    vulkan_image                ImageArray                  = {};
    vulkan_buffer               TextureBuffer               = {};
    vulkan_buffer               MaterialBuffer              = {};
    vulkan_buffer               ShapeBuffer                 = {};
    vulkan_buffer               ShapeNodeBuffer             = {};
    vulkan_buffer               MeshFaceBuffer              = {};
    vulkan_buffer               MeshVertexBuffer            = {};
    vulkan_buffer               MeshNodeBuffer              = {};

    VkDescriptorSetLayout       TraceDescriptorSetLayout    = VK_NULL_HANDLE;
    VkDescriptorSet             TraceDescriptorSet          = VK_NULL_HANDLE;
    vulkan_pipeline             TracePipeline               = {};
    vulkan_buffer               TraceBuffer                 = {};

    VkDescriptorSetLayout       PreviewDescriptorSetLayout  = VK_NULL_HANDLE;
    VkDescriptorSet             PreviewDescriptorSet        = VK_NULL_HANDLE;
    vulkan_pipeline             PreviewPipeline             = {};

    VkDescriptorSetLayout       ComputeDescriptorSetLayout  = VK_NULL_HANDLE;
    vulkan_pipeline             PathPipeline                = {};
    vulkan_image                SampleAccumulatorImage      = {};
    vulkan_buffer               PathBuffer                  = {};

    VkDescriptorSetLayout       ResolveDescriptorSetLayout  = VK_NULL_HANDLE;
    vulkan_pipeline             ResolvePipeline             = {};

    VkDescriptorSetLayout       ImGuiDescriptorSetLayout    = VK_NULL_HANDLE;
    vulkan_pipeline             ImGuiPipeline               = {};
    vulkan_image                ImGuiTexture                = {};
};

struct render_frame_parameters
{
    camera                      Camera = {};

    render_mode                 RenderMode                      = RENDER_MODE_PATH_TRACE;
    uint                        RenderFlags                     = 0;
    uint                        RenderSampleBlockSize           = 1;
    uint                        RenderBounceLimit               = 0;
    float                       RenderTerminationProbability    = 0.0f;
    bool                        RenderRestart                   = false;

    float                       Brightness                      = 1.0f;
    tone_mapping_mode           ToneMappingMode                 = TONE_MAPPING_MODE_CLAMP;
    float                       ToneMappingWhiteLevel           = 1.0f;

    ImDrawData*                 ImGuiDrawData                   = nullptr;
};

vulkan_context* CreateVulkan(
    GLFWwindow* Window,
    char const* ApplicationName);

void DestroyVulkan(
    vulkan_context* Vulkan);

VkResult UploadScene(
    vulkan_context* Vulkan,
    scene const* Scene,
    uint32_t Flags);

VkResult RenderFrame(
    vulkan_context* Vulkan,
    render_frame_parameters* Parameters);
