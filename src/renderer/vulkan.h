#pragma once

struct scene;

#include "core/common.h"
#include "scene/scene.h"

#include <vector>
#include <span>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

// Resources associated with a Vulkan buffer.
struct vulkan_buffer
{
    VkBuffer                    Buffer                      = VK_NULL_HANDLE;
    VkDeviceMemory              Memory                      = VK_NULL_HANDLE;
    VkDeviceSize                Size                        = 0;
};

// Resources associated with a Vulkan image.
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

// Description of the code and data bindings of a compute shader.
struct vulkan_compute_pipeline_configuration
{
    using descriptor_set_layouts = std::vector<VkDescriptorSetLayout>;

    std::span<uint32_t const>   ComputeShaderCode           = {};
    descriptor_set_layouts      DescriptorSetLayouts        = {};
    uint32_t                    PushConstantBufferSize      = 0;
};

// Resources associated with a Vulkan compute or graphics pipeline.
struct vulkan_pipeline
{
    VkPipeline                  Pipeline                    = VK_NULL_HANDLE;
    VkPipelineLayout            PipelineLayout              = VK_NULL_HANDLE;
};

// Simplified description of a descriptor for creating/updating descriptor sets.
struct vulkan_descriptor
{
    VkDescriptorType            Type                        = {};
    vulkan_buffer*              Buffer                      = nullptr;
    vulkan_image*               Image                       = nullptr;
    VkImageLayout               ImageLayout                 = VK_IMAGE_LAYOUT_GENERAL;
    VkSampler                   Sampler                     = VK_NULL_HANDLE;
};

// Vulkan resources and information required to track one in-flight frame.
struct vulkan_frame
{
    // For indexing external arrays of per-in-flight-frame resources.
    uint32_t                    Index                       = 0;

    // When true, this frame has not been in flight yet and cannot be waited on.
    bool                        Fresh                       = false;

    // Previous in-flight frame.
    vulkan_frame*               Previous                    = nullptr;

    // Signaled when the previous commands accessing the resources of this frame state have been completed.
    VkFence                     AvailableFence              = VK_NULL_HANDLE;

    // Index of the swap chain image to render to in this frame.
    uint32_t                    ImageIndex                  = 0;

    // Signaled when a swap chain image is ready to render into.
    VkSemaphore                 ImageAvailableSemaphore     = VK_NULL_HANDLE;

    // Signaled when rendering has finished and swap chain image can be presented.
    VkSemaphore                 ImageFinishedSemaphore      = VK_NULL_HANDLE;

    // Signaled when compute has finished for this frame, waited on by next frame compute.
    VkSemaphore                 ComputeToComputeSemaphore   = VK_NULL_HANDLE;

    // Signaled when compute has finished for this frame, waited on by this frame graphics.
    VkSemaphore                 ComputeToGraphicsSemaphore  = VK_NULL_HANDLE;

    // Command buffers for this frame.
    VkCommandBuffer             GraphicsCommandBuffer       = VK_NULL_HANDLE;
    VkCommandBuffer             ComputeCommandBuffer        = VK_NULL_HANDLE;

    // Resources for dynamic ImGui geometry submitted during this frame.
    vulkan_buffer               ImGuiIndexBuffer            = {};
    vulkan_buffer               ImGuiVertexBuffer           = {};
    VkDescriptorSet             ImGuiDescriptorSet          = {};
};

// Common resources associated with a Vulkan renderer instance.
struct vulkan
{
    // Vulkan instance.
    VkInstance                  Instance                    = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT    Messenger                   = VK_NULL_HANDLE;

    // Physical devices.
    VkPhysicalDevice            PhysicalDevice              = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures    PhysicalDeviceFeatures      = {};
    VkPhysicalDeviceProperties  PhysicalDeviceProperties    = {};

    // Logical device.
    VkDevice                    Device                      = VK_NULL_HANDLE;

    // Graphics queue.
    uint32_t                    GraphicsQueueFamilyIndex    = 0;
    VkQueue                     GraphicsQueue               = VK_NULL_HANDLE;
    VkCommandPool               GraphicsCommandPool         = VK_NULL_HANDLE;

    // Compute queue.
    uint32_t                    ComputeQueueFamilyIndex     = 0;
    VkQueue                     ComputeQueue                = VK_NULL_HANDLE;
    VkCommandPool               ComputeCommandPool          = VK_NULL_HANDLE;

    // Presentation queue.
    uint32_t                    PresentQueueFamilyIndex     = 0;
    VkQueue                     PresentQueue                = VK_NULL_HANDLE;
    VkPresentModeKHR            PresentMode                 = VK_PRESENT_MODE_IMMEDIATE_KHR;

    // Window resources.
    GLFWwindow*                 Window                      = nullptr;
    VkSurfaceKHR                Surface                     = VK_NULL_HANDLE;
    VkSurfaceFormatKHR          SurfaceFormat               = {};

    // Swap chain resources.
    VkSwapchainKHR              SwapChain                   = VK_NULL_HANDLE;
    VkExtent2D                  SwapChainExtent             = {};
    VkFormat                    SwapChainFormat             = VK_FORMAT_UNDEFINED;
    std::vector<VkImage>        SwapChainImages             = {};
    std::vector<VkImageView>    SwapChainImageViews         = {};
    std::vector<VkFramebuffer>  SwapChainFrameBuffers       = {};

    // Shared descriptor pool from which all descriptor sets are allocated.
    VkDescriptorPool            DescriptorPool              = VK_NULL_HANDLE;

    // Render pass for all graphics rendering operations.
    VkRenderPass                RenderPass                  = VK_NULL_HANDLE;

    // Resources per in-flight frame.
    uint32_t                    FrameIndex                  = 0;
    vulkan_frame                FrameStates[2]              = {};
    vulkan_frame*               CurrentFrame                = nullptr;

    // Texture samplers.
    VkSampler                   ImageSamplerNearestNoMip    = VK_NULL_HANDLE;
    VkSampler                   ImageSamplerLinear          = VK_NULL_HANDLE;
    VkSampler                   ImageSamplerLinearNoMip     = VK_NULL_HANDLE;

    // Descriptor set layout for all scene-related resources.
    VkDescriptorSetLayout       SceneDescriptorSetLayout    = VK_NULL_HANDLE;

    // Common resources for rendering a scene preview in edit mode.
    vulkan_pipeline             PreviewPipeline             = {};

    // Common resources for rendering a resolved view of a sample buffer.
    VkDescriptorSetLayout       ResolveDescriptorSetLayout  = VK_NULL_HANDLE;
    vulkan_pipeline             ResolvePipeline             = {};

    // Common resources for rendering ImGui.
    VkDescriptorSetLayout       ImGuiDescriptorSetLayout    = VK_NULL_HANDLE;
    vulkan_pipeline             ImGuiPipeline               = {};
    vulkan_image                ImGuiTexture                = {};

    // List of images that must be transitioned from compute-write
    // to fragment-read and back before/after the graphics render pass.
    std::vector<VkImage>        SharedImages                = {};
};

// A floating point sample accumulator buffer for Monte Carlo integration operations.
struct vulkan_sample_buffer
{
    VkDescriptorSet             ResolveDescriptorSet        = VK_NULL_HANDLE;
    vulkan_image                Image                       = {};
};

// Vulkan resources associated with a scene.
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

// Parameters for RenderSampleBuffer().
struct resolve_parameters
{
    float                       Brightness                      = 1.0f;
    tone_mapping_mode           ToneMappingMode                 = TONE_MAPPING_MODE_CLAMP;
    float                       ToneMappingWhiteLevel           = 1.0f;
};

enum preview_render_mode : uint
{
    PREVIEW_RENDER_MODE_BASE_COLOR              = 0,
    PREVIEW_RENDER_MODE_BASE_COLOR_SHADED       = 1,
    PREVIEW_RENDER_MODE_NORMAL                  = 2,
    PREVIEW_RENDER_MODE_MATERIAL_INDEX          = 3,
    PREVIEW_RENDER_MODE_PRIMITIVE_INDEX         = 4,
    PREVIEW_RENDER_MODE_MESH_COMPLEXITY         = 5,
    PREVIEW_RENDER_MODE_SCENE_COMPLEXITY        = 6,
    PREVIEW_RENDER_MODE__COUNT                  = 7,
};

inline char const* PreviewRenderModeName(preview_render_mode Mode)
{
    switch (Mode) {
    case PREVIEW_RENDER_MODE_BASE_COLOR:            return "Base Color";
    case PREVIEW_RENDER_MODE_BASE_COLOR_SHADED:     return "Base Color (Shaded)";
    case PREVIEW_RENDER_MODE_NORMAL:                return "Normal";
    case PREVIEW_RENDER_MODE_MATERIAL_INDEX:        return "Material ID";
    case PREVIEW_RENDER_MODE_PRIMITIVE_INDEX:       return "Primitive ID";
    case PREVIEW_RENDER_MODE_MESH_COMPLEXITY:       return "Mesh Complexity";
    case PREVIEW_RENDER_MODE_SCENE_COMPLEXITY:      return "Scene Complexity";
    }
    assert(false);
    return nullptr;
}

// Parameters for RenderPreview().
struct preview_parameters
{
    camera                      Camera;
    preview_render_mode         RenderMode;
    uint                        SelectedShapeIndex;
};

vulkan*         CreateVulkan(GLFWwindow* Window, char const* ApplicationName);
void            DestroyVulkan(vulkan* Vulkan);

VkResult        CreateBuffer(vulkan* Vulkan, vulkan_buffer* Buffer, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryFlags, VkDeviceSize Size);
void            DestroyBuffer(vulkan* Vulkan, vulkan_buffer* Buffer);
void            WriteToHostVisibleBuffer(vulkan* Vulkan, vulkan_buffer* Buffer, void const* Data, size_t Size);

VkResult        CreateDescriptorSetLayout(vulkan* Vulkan, VkDescriptorSetLayout* Layout, std::span<VkDescriptorType> DescriptorTypes);
void            WriteDescriptorSet(vulkan* Vulkan, VkDescriptorSet DescriptorSet, std::span<vulkan_descriptor> Descriptors);
VkResult        CreateDescriptorSet(vulkan* Vulkan, VkDescriptorSetLayout DescriptorSetLayout, VkDescriptorSet* DescriptorSet, std::span<vulkan_descriptor> Descriptors);

VkResult        CreateComputePipeline(vulkan* Vulkan, vulkan_pipeline* Pipeline, vulkan_compute_pipeline_configuration const& Config);
void            DestroyPipeline(vulkan* Vulkan, vulkan_pipeline* Pipeline);

vulkan_scene*   CreateVulkanScene(vulkan* Vulkan);
void            UpdateVulkanScene(vulkan* Vulkan, vulkan_scene* VulkanScene, scene* Scene, uint32_t Flags);
void            DestroyVulkanScene(vulkan* Vulkan, vulkan_scene* VulkanScene);

vulkan_sample_buffer* CreateSampleBuffer(vulkan* Vulkan, uint Width, uint Height);
void            DestroySampleBuffer(vulkan* Vulkan, vulkan_sample_buffer* SampleBuffer);

VkResult        BeginFrame(vulkan* Vulkan);
void            RenderSampleBuffer(vulkan* Vulkan, vulkan_sample_buffer* SampleBuffer, resolve_parameters* Parameters);
void            RenderPreview(vulkan* Vulkan, vulkan_scene* Scene, preview_parameters* Parameters);
void            RenderImGui(vulkan* Vulkan, vulkan_scene* Scene, ImDrawData* DrawData);
VkResult        EndFrame(vulkan* Vulkan);
