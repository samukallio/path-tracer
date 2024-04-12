#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

struct VulkanImage
{
    VkImage                     image                       = VK_NULL_HANDLE;
    VkImageView                 imageView                   = VK_NULL_HANDLE;
};

struct VulkanFrameState
{
    // Fence that gets signaled when the previous commands
    // accessing the resources of this frame state have been
    // completed.
    VkFence                     availableFence              = VK_NULL_HANDLE;

    // Swap chain image state for this frame.
    uint32_t                    imageIndex                  = 0;
    VkSemaphore                 imageAvailableSemaphore     = VK_NULL_HANDLE;
    VkSemaphore                 imageFinishedSemaphore      = VK_NULL_HANDLE;

    VkCommandBuffer             graphicsCommandBuffer       = VK_NULL_HANDLE;
    VkCommandBuffer             computeCommandBuffer        = VK_NULL_HANDLE;
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

    VkRenderPass                mainRenderPass              = VK_NULL_HANDLE;

    int                         frameIndex                  = 0;
    VulkanFrameState            frameStates[2]              = {};
};

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName);

void DestroyVulkan(
    VulkanContext* vulkan);

VkResult BeginFrame(
    VulkanContext* vulkan,
    VulkanFrameState** frameOut);

VkResult EndFrame(
    VulkanContext* vulkan,
    VulkanFrameState* frame);