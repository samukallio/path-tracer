#pragma once

#include <vector>
#include <span>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

struct VulkanImage
{
    VkImage                     image                       = VK_NULL_HANDLE;
    VkDeviceMemory              memory                      = VK_NULL_HANDLE;
    VkImageView                 view                        = VK_NULL_HANDLE;
    VkImageType                 type                        = VK_IMAGE_TYPE_1D;
    VkFormat                    format                      = VK_FORMAT_UNDEFINED;
    VkExtent3D                  extent                      = {};
    VkImageTiling               tiling                      = VK_IMAGE_TILING_OPTIMAL;
};

struct VulkanDescriptor
{
    VkDescriptorBufferInfo      buffer                      = {};
    VkDescriptorImageInfo       image                       = {};
};

struct VulkanPipeline
{
    using Bindings = std::vector<VkDescriptorSetLayoutBinding>;

    VkPipeline                  pipeline                    = VK_NULL_HANDLE;
    VkPipelineLayout            pipelineLayout              = VK_NULL_HANDLE;
    Bindings                    descriptorSetLayoutBindings = {};
    VkDescriptorSetLayout       descriptorSetLayout         = VK_NULL_HANDLE;
    VkDescriptorSet             descriptorSets[2]           = {};
    VkPipelineBindPoint         bindPoint                   = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

struct VulkanFrameState
{
    uint32_t                    index                       = 0;
    bool                        fresh                       = false;

    // Fence that gets signaled when the previous commands
    // accessing the resources of this frame state have been
    // completed.
    VkFence                     availableFence              = VK_NULL_HANDLE;

    // Swap chain image state for this frame.
    uint32_t                    imageIndex                  = 0;
    VkSemaphore                 imageAvailableSemaphore     = VK_NULL_HANDLE;
    VkSemaphore                 imageFinishedSemaphore      = VK_NULL_HANDLE;

    VkSemaphore                 computeToComputeSemaphore   = VK_NULL_HANDLE;
    VkSemaphore                 computeToGraphicsSemaphore  = VK_NULL_HANDLE;

    VkCommandBuffer             graphicsCommandBuffer       = VK_NULL_HANDLE;
    VkCommandBuffer             computeCommandBuffer        = VK_NULL_HANDLE;

    VulkanImage                 renderTarget                = {};
    VulkanImage                 renderTargetGraphicsCopy    = {};
};

struct VulkanContext
{
    VkInstance                  instance                    = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT    messenger                   = VK_NULL_HANDLE;

    VkPhysicalDevice            physicalDevice              = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures    physicalDeviceFeatures      = {};
    VkPhysicalDeviceProperties  physicalDeviceProperties    = {};

    VkDevice                    device                      = VK_NULL_HANDLE;

    uint32_t                    graphicsQueueFamilyIndex    = 0;
    VkQueue                     graphicsQueue               = VK_NULL_HANDLE;
    VkCommandPool               graphicsCommandPool         = VK_NULL_HANDLE;

    uint32_t                    computeQueueFamilyIndex     = 0;
    VkQueue                     computeQueue                = VK_NULL_HANDLE;
    VkCommandPool               computeCommandPool          = VK_NULL_HANDLE;

    uint32_t                    presentQueueFamilyIndex     = 0;
    VkQueue                     presentQueue                = VK_NULL_HANDLE;
    VkPresentModeKHR            presentMode                 = VK_PRESENT_MODE_IMMEDIATE_KHR;

    GLFWwindow*                 window                      = nullptr;
    VkSurfaceKHR                surface                     = VK_NULL_HANDLE;
    VkSurfaceFormatKHR          surfaceFormat               = {};

    VkSwapchainKHR              swapchain                   = VK_NULL_HANDLE;
    VkExtent2D                  swapchainExtent             = {};
    VkFormat                    swapchainFormat             = VK_FORMAT_UNDEFINED;
    std::vector<VkImage>        swapchainImages             = {};
    std::vector<VkImageView>    swapchainImageViews         = {};
    std::vector<VkFramebuffer>  swapchainFramebuffers       = {};

    VkDescriptorPool            descriptorPool              = VK_NULL_HANDLE;

    VkRenderPass                mainRenderPass              = VK_NULL_HANDLE;

    int                         frameIndex                  = 0;
    VulkanFrameState            frameStates[2]              = {};

    VulkanPipeline              blitPipeline                = {};
    VulkanPipeline              renderPipeline              = {};
};

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName);

void DestroyVulkan(
    VulkanContext* vulkan);

VkResult RenderFrame(
    VulkanContext* vulkan);
