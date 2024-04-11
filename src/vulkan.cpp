#include "vulkan.h"

#include <cstdarg>
#include <vector>
#include <set>
#include <algorithm>
#include <optional>

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

static VkResult InternalCreatePresentationResources(
    VulkanContext* vulkan);

static VkResult InternalCreateVulkan(
    VulkanContext* vulkan,
    GLFWwindow* window,
    char const* applicationName)
{
    VkResult result = VK_SUCCESS;

    std::vector<char const*> requiredExtensionNames = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    std::vector<char const*> requiredLayerNames = { "VK_LAYER_KHRONOS_validation" };
    std::vector<char const*> requiredDeviceExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    // Gather Vulkan extensions required by GLFW.
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        requiredExtensionNames.reserve(glfwExtensionCount);
        for (uint32_t k = 0; k < glfwExtensionCount; k++)
            requiredExtensionNames.push_back(glfwExtensions[k]);
    }

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

    // Create Vulkan instance.
    {
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
    }

    // Create window surface.
    {
        result = glfwCreateWindowSurface(vulkan->instance, window, nullptr, &vulkan->surface);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create window surface");
            return result;
        }
        vulkan->window = window;
    }

    // Enumerate physical devices and find the most suitable one.
    {
        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(vulkan->instance, &physicalDeviceCount, nullptr);
        auto physicalDevices = std::vector<VkPhysicalDevice>(physicalDeviceCount);
        vkEnumeratePhysicalDevices(vulkan->instance, &physicalDeviceCount, physicalDevices.data());

        for (VkPhysicalDevice physicalDevice : physicalDevices) {
            // Find the required queue families.
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

            std::optional<uint32_t> graphicsQueueFamilyIndex;
            std::optional<uint32_t> computeQueueFamilyIndex;
            std::optional<uint32_t> presentQueueFamilyIndex;

            for (uint32_t index = 0; index < queueFamilyCount; index++) {
                auto const& queueFamily = queueFamilies[index];

                if (!graphicsQueueFamilyIndex.has_value()) {
                    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        graphicsQueueFamilyIndex = index;
                }

                if (!computeQueueFamilyIndex.has_value()) {
                    if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                        computeQueueFamilyIndex = index;
                }

                if (!presentQueueFamilyIndex.has_value()) {
                    VkBool32 presentSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, index, vulkan->surface, &presentSupport);
                    if (presentSupport) presentQueueFamilyIndex = index;
                }
            }

            if (!graphicsQueueFamilyIndex.has_value())
                continue;
            if (!computeQueueFamilyIndex.has_value())
                continue;
            if (!presentQueueFamilyIndex.has_value())
                continue;

            // Ensure the requested device extensions are supported.
            uint32_t deviceExtensionCount;
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
            auto deviceExtensions = std::vector<VkExtensionProperties>(deviceExtensionCount);
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, deviceExtensions.data());

            bool deviceExtensionsFound = true;
            for (char const* extensionName : requiredDeviceExtensionNames) {
                bool found = false;
                for (VkExtensionProperties const& extension : deviceExtensions) {
                    found = !strcmp(extension.extensionName, extensionName);
                    if (found) break;
                }
                if (!found) {
                    deviceExtensionsFound = false;
                    break;
                }
            }
            if (!deviceExtensionsFound)
                continue;

            // Find suitable surface format for the swap chain.
            uint32_t surfaceFormatCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vulkan->surface, &surfaceFormatCount, nullptr);
            auto surfaceFormats = std::vector<VkSurfaceFormatKHR>(surfaceFormatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vulkan->surface, &surfaceFormatCount, surfaceFormats.data());

            VkSurfaceFormatKHR surfaceFormat = {};
            bool surfaceFormatFound = false;
            for (VkSurfaceFormatKHR const& sf : surfaceFormats) {
                if (sf.format == VK_FORMAT_B8G8R8A8_SRGB &&
                    sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    surfaceFormat = sf;
                    surfaceFormatFound = true;
                }
            }
            if (!surfaceFormatFound)
                continue;

            // Choose a suitable present mode.
            uint32_t presentModeCount = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vulkan->surface, &presentModeCount, nullptr);
            auto presentModes = std::vector<VkPresentModeKHR>(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vulkan->surface, &presentModeCount, presentModes.data());

            VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
            for (auto const& pm : presentModes) {
                if (pm == VK_PRESENT_MODE_MAILBOX_KHR)
                    presentMode = pm;
            }

            // Check physical device features and properties.
            VkPhysicalDeviceFeatures physicalDeviceFeatures;
            vkGetPhysicalDeviceFeatures(physicalDevice, &physicalDeviceFeatures);
            VkPhysicalDeviceProperties physicalDeviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

            // Suitable physical device found.
            vulkan->physicalDevice = physicalDevice;
            vulkan->physicalDeviceFeatures = physicalDeviceFeatures;
            vulkan->physicalDeviceProperties = physicalDeviceProperties;
            vulkan->graphicsQueueFamilyIndex = graphicsQueueFamilyIndex.value();
            vulkan->computeQueueFamilyIndex = computeQueueFamilyIndex.value();
            vulkan->presentQueueFamilyIndex = presentQueueFamilyIndex.value();
            vulkan->surfaceFormat = surfaceFormat;
            vulkan->presentMode = presentMode;
            break;
        }

        if (vulkan->physicalDevice == VK_NULL_HANDLE) {
            Errorf(vulkan, "no suitable physical device");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    // Create logical device.
    {
        const float queuePriority = 1.0f;

        auto deviceFeatures = VkPhysicalDeviceFeatures {
            .samplerAnisotropy = VK_TRUE,
        };

        std::set<uint32_t> queueFamilyIndices = {
            vulkan->graphicsQueueFamilyIndex,
            vulkan->computeQueueFamilyIndex,
            vulkan->presentQueueFamilyIndex,
        };

        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (uint32_t queueFamilyIndex : queueFamilyIndices) {
            queueInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = queueFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            });
        }

        auto deviceCreateInfo = VkDeviceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = (uint32_t)queueInfos.size(),
            .pQueueCreateInfos = queueInfos.data(),
            .enabledLayerCount = (uint32_t)requiredLayerNames.size(),
            .ppEnabledLayerNames = requiredLayerNames.data(),
            .enabledExtensionCount = (uint32_t)requiredDeviceExtensionNames.size(),
            .ppEnabledExtensionNames = requiredDeviceExtensionNames.data(),
            .pEnabledFeatures = &deviceFeatures,
        };

        result = vkCreateDevice(vulkan->physicalDevice, &deviceCreateInfo, nullptr, &vulkan->device);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create device");
            return result;
        }

        vkGetDeviceQueue(vulkan->device, vulkan->graphicsQueueFamilyIndex, 0, &vulkan->graphicsQueue);
        vkGetDeviceQueue(vulkan->device, vulkan->computeQueueFamilyIndex, 0, &vulkan->computeQueue);
        vkGetDeviceQueue(vulkan->device, vulkan->presentQueueFamilyIndex, 0, &vulkan->presentQueue);
    }

    // Create graphics and compute command pools.
    {
        auto graphicsCommandPoolInfo = VkCommandPoolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = vulkan->graphicsQueueFamilyIndex,
        };

        result = vkCreateCommandPool(vulkan->device, &graphicsCommandPoolInfo, nullptr, &vulkan->graphicsCommandPool);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create graphics command pool");
            return result;
        }

        auto computeCommandPoolInfo = VkCommandPoolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = vulkan->computeQueueFamilyIndex,
        };

        result = vkCreateCommandPool(vulkan->device, &computeCommandPoolInfo, nullptr, &vulkan->computeCommandPool);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create compute command pool");
            return result;
        }
    }

    result = InternalCreatePresentationResources(vulkan);
    if (result != VK_SUCCESS) return result;

    return VK_SUCCESS;
}

VulkanContext* CreateVulkan(
    GLFWwindow* window,
    char const* applicationName)
{
    auto vulkan = new VulkanContext;

    if (InternalCreateVulkan(vulkan, window, applicationName) != VK_SUCCESS) {
        DestroyVulkan(vulkan);
        delete vulkan;
        vulkan = nullptr;
    }

    return vulkan;
}

static VkResult InternalCreatePresentationResources(
    VulkanContext* vulkan)
{
    VkResult result = VK_SUCCESS;

    // Create the swap chain.
    {
        // Determine current window surface capabilities.
        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->physicalDevice, vulkan->surface, &surfaceCapabilities);

        // Determine width and height of the swap chain.
        VkExtent2D imageExtent = surfaceCapabilities.currentExtent;
        if (imageExtent.width == 0xFFFFFFFF) {
            int width, height;
            glfwGetFramebufferSize(vulkan->window, &width, &height);
            imageExtent.width = std::clamp(
                static_cast<uint32_t>(width),
                surfaceCapabilities.minImageExtent.width,
                surfaceCapabilities.maxImageExtent.width);
            imageExtent.height = std::clamp(
                static_cast<uint32_t>(height),
                surfaceCapabilities.minImageExtent.height,
                surfaceCapabilities.maxImageExtent.height);
        }

        // Determine swap chain image count.
        uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
        if (surfaceCapabilities.maxImageCount > 0)
            imageCount = std::min(imageCount, surfaceCapabilities.maxImageCount);

        auto swapchainInfo = VkSwapchainCreateInfoKHR {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = vulkan->surface,
            .minImageCount = imageCount,
            .imageFormat = vulkan->surfaceFormat.format,
            .imageColorSpace = vulkan->surfaceFormat.colorSpace,
            .imageExtent = imageExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = vulkan->presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        if (vulkan->graphicsQueueFamilyIndex == vulkan->presentQueueFamilyIndex) {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainInfo.queueFamilyIndexCount = 0;
            swapchainInfo.pQueueFamilyIndices = nullptr;
        }
        else {
            uint32_t const queueFamilyIndices[] = {
                vulkan->graphicsQueueFamilyIndex,
                vulkan->presentQueueFamilyIndex
            };
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainInfo.queueFamilyIndexCount = 2;
            swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
        }

        result = vkCreateSwapchainKHR(vulkan->device, &swapchainInfo, nullptr, &vulkan->swapchain);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create swap chain");
            return result;
        }

        vulkan->swapchainExtent = imageExtent;
        vulkan->swapchainFormat = swapchainInfo.imageFormat;
    }

    // Retrieve swap chain images.
    {
        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(vulkan->device, vulkan->swapchain, &imageCount, nullptr);
        auto images = std::vector<VkImage>(imageCount);
        vkGetSwapchainImagesKHR(vulkan->device, vulkan->swapchain, &imageCount, images.data());

        vulkan->swapchainImages.clear();
        for (VkImage image : images) {
            auto imageViewInfo = VkImageViewCreateInfo {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = vulkan->swapchainFormat,
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };

            VkImageView imageView;

            result = vkCreateImageView(vulkan->device, &imageViewInfo, nullptr, &imageView);
            if (result != VK_SUCCESS) {
                Errorf(vulkan, "failed to create image view");
                return result;
            }

            vulkan->swapchainImages.push_back({
                .image = image,
                .imageView = imageView,
            });
        }
    }

    return VK_SUCCESS;
}

static void InternalDestroyPresentationResources(
    VulkanContext* vulkan)
{
    for (VulkanImage const& image : vulkan->swapchainImages)
        vkDestroyImageView(vulkan->device, image.imageView, nullptr);
    vulkan->swapchainImages.clear();

    if (vulkan->swapchain) {
        vkDestroySwapchainKHR(vulkan->device, vulkan->swapchain, nullptr);
        vulkan->swapchain = nullptr;
        vulkan->swapchainExtent = {};
        vulkan->swapchainFormat = VK_FORMAT_UNDEFINED;
    }
}

void DestroyVulkan(VulkanContext* vulkan)
{
    if (vulkan->device) {
        // Device exists, make sure there is nothing going on
        // before we start releasing resources.
        vkDeviceWaitIdle(vulkan->device);
    }

    // Destroy swap chain and any other window-related resources.
    InternalDestroyPresentationResources(vulkan);

    if (vulkan->graphicsCommandPool) {
        vkDestroyCommandPool(vulkan->device, vulkan->graphicsCommandPool, nullptr);
        vulkan->graphicsCommandPool = VK_NULL_HANDLE;
    }

    if (vulkan->computeCommandPool) {
        vkDestroyCommandPool(vulkan->device, vulkan->computeCommandPool, nullptr);
        vulkan->computeCommandPool = VK_NULL_HANDLE;
    }

    if (vulkan->device) {
        vkDestroyDevice(vulkan->device, nullptr);
        vulkan->device = nullptr;
        vulkan->graphicsQueue = VK_NULL_HANDLE;
        vulkan->computeQueue = VK_NULL_HANDLE;
        vulkan->presentQueue = VK_NULL_HANDLE;
    }

    if (vulkan->physicalDevice) {
        vulkan->physicalDevice = VK_NULL_HANDLE;
        vulkan->physicalDeviceFeatures = {};
        vulkan->physicalDeviceProperties = {};
        vulkan->graphicsQueueFamilyIndex = 0;
        vulkan->computeQueueFamilyIndex = 0;
        vulkan->presentQueueFamilyIndex = 0;
        vulkan->surfaceFormat = {};
        vulkan->presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    if (vulkan->messenger) {
        DestroyDebugUtilsMessengerEXT(vulkan->instance, vulkan->messenger, nullptr);
        vulkan->messenger = VK_NULL_HANDLE;
    }

    if (vulkan->surface) {
        vkDestroySurfaceKHR(vulkan->instance, vulkan->surface, nullptr);
        vulkan->surface = VK_NULL_HANDLE;
        vulkan->window = nullptr;
    }

    if (vulkan->instance) {
        vkDestroyInstance(vulkan->instance, nullptr);
        vulkan->instance = VK_NULL_HANDLE;
    }
}
