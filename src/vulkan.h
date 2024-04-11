#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

struct VulkanImage
{
    VkImage                     image                       = VK_NULL_HANDLE;
    VkImageView                 imageView                   = VK_NULL_HANDLE;
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

    GLFWwindow*                 window                      = nullptr;
    VkSurfaceKHR                surface                     = VK_NULL_HANDLE;
    VkSurfaceFormatKHR          surfaceFormat               = {};

    VkSwapchainKHR              swapchain                   = VK_NULL_HANDLE;
    VkExtent2D                  swapchainExtent             = {};
    VkFormat                    swapchainFormat             = VK_FORMAT_UNDEFINED;
    std::vector<VulkanImage>    swapchainImages             = {};

    uint32_t                    presentQueueFamilyIndex     = 0;
    VkQueue                     presentQueue                = VK_NULL_HANDLE;
    VkPresentModeKHR            presentMode                 = VK_PRESENT_MODE_IMMEDIATE_KHR;
};

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName);

void DestroyVulkan(
    VulkanContext* vulkan);
