#pragma once

struct scene;

#include "core/common.hpp"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

struct vulkan_buffer
{
    VkBuffer       Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize   Size = 0;
    bool           IsDeviceLocal = false;
};

struct vulkan_image
{
    VkImage        Image      = VK_NULL_HANDLE;
    VkDeviceMemory Memory     = VK_NULL_HANDLE;
    VkImageView    View       = VK_NULL_HANDLE;
    VkImageType    Type       = VK_IMAGE_TYPE_1D;
    VkFormat       Format     = VK_FORMAT_UNDEFINED;
    VkExtent3D     Extent     = {};
    VkImageTiling  Tiling     = VK_IMAGE_TILING_OPTIMAL;
    uint32_t       LayerCount = 0;
};

struct vulkan_compute_pipeline_configuration
{
    using descriptor_set_layouts = std::vector<VkDescriptorSetLayout>;

    std::span<uint32_t const> ComputeShaderCode      = {};
    descriptor_set_layouts    DescriptorSetLayouts   = {};
    uint32_t                  PushConstantBufferSize = 0;
};

struct vulkan_graphics_pipeline_configuration
{
    using vertex_format = std::vector<VkVertexInputAttributeDescription>;
    using descriptor_set_layouts = std::vector<VkDescriptorSetLayout>;

    uint32_t                  VertexSize             = 0;
    vertex_format             VertexFormat           = {};
    std::span<uint32_t const> VertexShaderCode       = {};
    std::span<uint32_t const> FragmentShaderCode     = {};
    descriptor_set_layouts    DescriptorSetLayouts   = {};
    uint32_t                  PushConstantBufferSize = 0;
};

struct vulkan_pipeline
{
    VkPipeline       Pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
};

// Simplified description of a descriptor for creating/updating descriptor sets.
struct vulkan_descriptor
{
    VkDescriptorType Type        = {};
    vulkan_buffer*   Buffer      = nullptr;
    vulkan_image*    Image       = nullptr;
    VkImageLayout    ImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkSampler        Sampler     = VK_NULL_HANDLE;
};

// Vulkan resources and information required to track one in-flight frame.
struct vulkan_frame
{
    // For indexing external arrays of per-in-flight-frame resources.
    uint32_t Index = 0;

    // When true, this frame has not been in flight yet and cannot be waited on.
    bool Fresh = false;
    // Previous in-flight frame.
    vulkan_frame* Previous = nullptr;

    // Index of the swap chain image to render to in this frame.
    uint32_t ImageIndex = 0;

    // Signaled when compute has finished for this frame, waited on by next frame compute.
    VkSemaphore ComputeToComputeSemaphore = VK_NULL_HANDLE;
    // Signaled when compute has finished for this frame, waited on by this frame graphics.
    VkSemaphore ComputeToGraphicsSemaphore = VK_NULL_HANDLE;
    // Signaled when a swap chain image is ready to render into.
    VkSemaphore PresentToGraphicsSemaphore = VK_NULL_HANDLE;
    // Signaled when rendering has finished and swap chain image can be presented.
    VkSemaphore GraphicsToPresentSemaphore = VK_NULL_HANDLE;
    // Signaled when the previous commands accessing the resources of this frame state have been completed.
    VkFence AvailableFence = VK_NULL_HANDLE;

    // Command buffers for this frame.
    VkCommandBuffer GraphicsCommandBuffer = VK_NULL_HANDLE;
    VkCommandBuffer ComputeCommandBuffer  = VK_NULL_HANDLE;
};

// Common resources associated with a Vulkan renderer instance.
struct vulkan
{
    // Vulkan instance.
    VkInstance               Instance  = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT Messenger = VK_NULL_HANDLE;

    // Physical devices.
    VkPhysicalDevice           PhysicalDevice           = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures   PhysicalDeviceFeatures   = {};
    VkPhysicalDeviceProperties PhysicalDeviceProperties = {};

    // Logical device.
    VkDevice Device = VK_NULL_HANDLE;

    // Graphics queue.
    uint32_t      GraphicsQueueFamilyIndex = 0;
    VkQueue       GraphicsQueue            = VK_NULL_HANDLE;
    VkCommandPool GraphicsCommandPool      = VK_NULL_HANDLE;

    // Compute queue.
    uint32_t      ComputeQueueFamilyIndex = 0;
    VkQueue       ComputeQueue            = VK_NULL_HANDLE;
    VkCommandPool ComputeCommandPool      = VK_NULL_HANDLE;

    // Presentation queue.
    uint32_t         PresentQueueFamilyIndex = 0;
    VkQueue          PresentQueue            = VK_NULL_HANDLE;
    VkPresentModeKHR PresentMode             = VK_PRESENT_MODE_IMMEDIATE_KHR;

    // Window resources.
    GLFWwindow*        Window        = nullptr;
    VkSurfaceKHR       Surface       = VK_NULL_HANDLE;
    VkSurfaceFormatKHR SurfaceFormat = {};

    // Swap chain resources.
    VkSwapchainKHR             SwapChain             = VK_NULL_HANDLE;
    VkExtent2D                 SwapChainExtent       = {};
    VkFormat                   SwapChainFormat       = VK_FORMAT_UNDEFINED;
    std::vector<VkImage>       SwapChainImages       = {};
    std::vector<VkImageView>   SwapChainImageViews   = {};
    std::vector<VkFramebuffer> SwapChainFrameBuffers = {};

    // Shared descriptor pool from which all descriptor sets are allocated.
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;

    // Render pass for all graphics rendering operations.
    VkRenderPass RenderPass = VK_NULL_HANDLE;

    // Resources per in-flight frame.
    uint32_t      FrameIndex   = 0;
    vulkan_frame  Frames[2]    = {};
    vulkan_frame* CurrentFrame = nullptr;

    // Texture samplers.
    VkSampler ImageSamplerNearestNoMip = VK_NULL_HANDLE;
    VkSampler ImageSamplerLinear       = VK_NULL_HANDLE;
    VkSampler ImageSamplerLinearNoMip  = VK_NULL_HANDLE;

    // List of images that must be transitioned from compute-write
    // to fragment-read and back before/after the graphics render pass.
    std::vector<VkImage> SharedImages = {};
};


vulkan* CreateVulkan(GLFWwindow* Window, char const* ApplicationName);

void DestroyVulkan(vulkan* Vulkan);

VkResult CreateVulkanBuffer
(
    vulkan*               Vulkan,
    vulkan_buffer*        Buffer,
    VkBufferUsageFlags    UsageFlags,
    VkMemoryPropertyFlags MemoryFlags,
    VkDeviceSize          Size
);

void DestroyVulkanBuffer(vulkan* Vulkan, vulkan_buffer* Buffer);

void WriteToVulkanBuffer
(
    vulkan*        Vulkan,
    vulkan_buffer* Buffer,
    void const*    Data,
    size_t         Size
);

VkResult CreateVulkanImage
(
    vulkan*               Vulkan,
    vulkan_image*         Image,
    VkImageUsageFlags     UsageFlags,
    VkMemoryPropertyFlags MemoryFlags,
    VkImageType           Type,
    VkFormat              Format,
    VkExtent3D            Extent,
    uint32_t              LayerCount,
    VkImageTiling         Tiling,
    VkImageLayout         Layout,
    bool                  Compute
);

void DestroyVulkanImage(vulkan* Vulkan, vulkan_image* Image);

VkResult WriteToVulkanImage
(
    vulkan*       Vulkan,
    vulkan_image* Image,
    uint32_t      LayerIndex,
    uint32_t      LayerCount,
    void const*   Data,
    uint32_t      Width,
    uint32_t      Height,
    uint32_t      BytesPerPixel,
    VkImageLayout NewLayout
);

VkResult CreateVulkanDescriptorSetLayout
(
    vulkan*                     Vulkan,
    VkDescriptorSetLayout*      Layout,
    std::span<VkDescriptorType> DescriptorTypes
);

void DestroyVulkanDescriptorSetLayout(vulkan* Vulkan, VkDescriptorSetLayout* Layout);

void UpdateVulkanDescriptorSet
(
    vulkan*                      Vulkan,
    VkDescriptorSet              DescriptorSet,
    std::span<vulkan_descriptor> Descriptors
);

VkResult CreateVulkanDescriptorSet
(
    vulkan*                      Vulkan,
    VkDescriptorSetLayout        DescriptorSetLayout,
    VkDescriptorSet*             DescriptorSet,
    std::span<vulkan_descriptor> Descriptors
);

void DestroyVulkanDescriptorSet(vulkan* Vulkan, VkDescriptorSet* Set);

VkResult CreateVulkanComputePipeline
(
    vulkan* Vulkan,
    vulkan_pipeline* Pipeline,
    vulkan_compute_pipeline_configuration const& Config
);

VkResult CreateVulkanGraphicsPipeline
(
    vulkan* Vulkan,
    vulkan_pipeline* Pipeline,
    vulkan_graphics_pipeline_configuration const& Config
);

void DestroyVulkanPipeline(vulkan* Vulkan, vulkan_pipeline* Pipeline);

VkResult BeginVulkanFrame(vulkan* Vulkan);

VkResult EndVulkanFrame(vulkan* Vulkan);
