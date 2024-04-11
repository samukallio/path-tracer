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
    VkSurfaceKHR                windowSurface               = VK_NULL_HANDLE;
};

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName);

void DestroyVulkan(VulkanContext* vk);
