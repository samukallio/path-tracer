#include "vulkan.h"

#include <cstdarg>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

static void Errorf(VulkanContext* vk, char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    Errorf(static_cast<VulkanContext*>(pUserData), pCallbackData->pMessage);
    return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!func) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
}

static void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (!func) return;
    func(instance, debugMessenger, pAllocator);
}

static VkResult InitializeVulkan(
    VulkanContext* vulkan,
    GLFWwindow* window,
    char const* applicationName)
{
    VkResult result = VK_SUCCESS;

    // Gather required Vulkan extensions.
    std::vector<char const*> requiredExtensionNames = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        requiredExtensionNames.reserve(glfwExtensionCount);
        for (uint32_t k = 0; k < glfwExtensionCount; k++)
            requiredExtensionNames.push_back(glfwExtensions[k]);
    }

    // Required validation layers.
    std::vector<char const*> requiredLayerNames = {
        "VK_LAYER_KHRONOS_validation",
    };

    // Check support for validation layers.
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

        for (char const* layerName : requiredLayerNames) {
            bool found = false;
            for (VkLayerProperties const& layer : layers) {
                found = !strcmp(layer.layerName, layerName);
                if (found) break;
            }
            if (!found) {
                Errorf(vulkan, "layer '%s' not found", layerName);
                return VK_ERROR_LAYER_NOT_PRESENT;
            }
        }
    }

    auto debugMessengerInfo = VkDebugUtilsMessengerCreateInfoEXT {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity
            = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType
            = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = VulkanDebugCallback,
        .pUserData = vulkan,
    };

    auto applicationInfo = VkApplicationInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationName,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = nullptr,
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    auto instanceInfo = VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &debugMessengerInfo,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = (uint32_t)requiredLayerNames.size(),
        .ppEnabledLayerNames = requiredLayerNames.data(),
        .enabledExtensionCount = (uint32_t)requiredExtensionNames.size(),
        .ppEnabledExtensionNames = requiredExtensionNames.data(),
    };

    result = vkCreateInstance(&instanceInfo, nullptr, &vulkan->instance);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create instance");
        return result;
    }

    result = CreateDebugUtilsMessengerEXT(vulkan->instance, &debugMessengerInfo, nullptr, &vulkan->messenger);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create debug messenger");
        return result;
    }

    result = glfwCreateWindowSurface(vulkan->instance, window, nullptr, &vulkan->windowSurface);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create window surface");
        return result;
    }
    vulkan->window = window;

    return VK_SUCCESS;
}

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName)
{
    auto vulkan = new VulkanContext;

    if (InitializeVulkan(vulkan, window, applicationName) != VK_SUCCESS) {
        DestroyVulkan(vulkan);
        delete vulkan;
        vulkan = nullptr;
    }

    return vulkan;
}

void DestroyVulkan(VulkanContext* vulkan)
{
    if (vulkan->messenger) {
        DestroyDebugUtilsMessengerEXT(vulkan->instance, vulkan->messenger, nullptr);
        vulkan->messenger = VK_NULL_HANDLE;
    }

    if (vulkan->windowSurface) {
        vkDestroySurfaceKHR(vulkan->instance, vulkan->windowSurface, nullptr);
        vulkan->windowSurface = VK_NULL_HANDLE;
        vulkan->window = nullptr;
    }

    if (vulkan->instance) {
        vkDestroyInstance(vulkan->instance, nullptr);
        vulkan->instance = VK_NULL_HANDLE;
    }

}
