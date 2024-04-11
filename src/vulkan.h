#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

struct VulkanContext
{
    VkInstance                  instance                    = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT    messenger                   = VK_NULL_HANDLE;

    VkPhysicalDevice            physicalDevice              = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures    physicalDeviceFeatures      = {};
    VkPhysicalDeviceProperties  physicalDeviceProperties    = {};

    VkDevice                    device                      = VK_NULL_HANDLE;
    GLFWwindow*                 window                      = nullptr;

    VkSurfaceKHR                surface                     = VK_NULL_HANDLE;
    VkSurfaceFormatKHR          surfaceFormat               = {};

    uint32_t                    graphicsQueueFamilyIndex    = 0;
    VkQueue                     graphicsQueue               = VK_NULL_HANDLE;

    uint32_t                    computeQueueFamilyIndex     = 0;
    VkQueue                     computeQueue                = VK_NULL_HANDLE;

    uint32_t                    presentQueueFamilyIndex     = 0;
    VkQueue                     presentQueue                = VK_NULL_HANDLE;
    VkPresentModeKHR            presentMode                 = VK_PRESENT_MODE_IMMEDIATE_KHR;
};

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName);

void DestroyVulkan(VulkanContext* vk);
