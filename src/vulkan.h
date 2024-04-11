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
    uint32_t                    computeQueueFamilyIndex     = 0;
    uint32_t                    presentQueueFamilyIndex     = 0;
    VkPresentModeKHR            presentMode                 = VK_PRESENT_MODE_IMMEDIATE_KHR;
};

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName);

void DestroyVulkan(VulkanContext* vk);
