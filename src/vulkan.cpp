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


static VkResult InternalCreateFrameResources(
    VulkanContext* vulkan,
    VulkanFrameState* frame)
{
    VkResult result = VK_SUCCESS;

    auto graphicsCommandBufferAllocateInfo = VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vulkan->graphicsCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    result = vkAllocateCommandBuffers(vulkan->device, &graphicsCommandBufferAllocateInfo, &frame->graphicsCommandBuffer);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to allocate graphics command buffer");
        return result;
    }

    auto computeCommandBufferAllocateInfo = VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vulkan->computeCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    result = vkAllocateCommandBuffers(vulkan->device, &computeCommandBufferAllocateInfo, &frame->computeCommandBuffer);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to allocate compute command buffer");
        return result;
    }

    auto semaphoreInfo = VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    VkSemaphore* pSemaphores[] = {
        &frame->imageAvailableSemaphore,
        &frame->imageFinishedSemaphore,
    };

    for (VkSemaphore* pSemaphore : pSemaphores) {
        result = vkCreateSemaphore(vulkan->device, &semaphoreInfo, nullptr, pSemaphore);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create semaphore");
            return result;
        }
    }

    auto fenceInfo = VkFenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    VkFence* pFences[] = {
        &frame->availableFence,
    };

    for (VkFence* pFence : pFences) {
        result = vkCreateFence(vulkan->device, &fenceInfo, nullptr, pFence);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create semaphore");
            return result;
        }
    }

    return VK_SUCCESS;
}

static VkResult InternalDestroyFrameResources(
    VulkanContext* vulkan,
    VulkanFrameState* frame)
{
    vkDestroySemaphore(vulkan->device, frame->imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(vulkan->device, frame->imageFinishedSemaphore, nullptr);
    vkDestroyFence(vulkan->device, frame->availableFence, nullptr);

    return VK_SUCCESS;
}

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

    // Create descriptor pool.
    {
        VkDescriptorPoolSize descriptorPoolSizes[] = {
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 16,
            },
            {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 16,
            },
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 16,
            },
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 16,
            .poolSizeCount = 3,
            .pPoolSizes = descriptorPoolSizes,
        };

        result = vkCreateDescriptorPool(vulkan->device, &descriptorPoolInfo, nullptr, &vulkan->descriptorPool);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create descriptor pool");
            return result;
        }
    }

    // Create main render pass.
    {
        VkAttachmentDescription colorAttachment = {
            .format = vulkan->surfaceFormat.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

        VkAttachmentReference colorAttachmentRef = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        VkSubpassDescription subpassDesc = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = nullptr,
        };

        VkSubpassDependency dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        VkRenderPassCreateInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorAttachment,
            .subpassCount = 1,
            .pSubpasses = &subpassDesc,
            .dependencyCount = 1,
            .pDependencies = &dependency,
        };

        result = vkCreateRenderPass(vulkan->device, &renderPassInfo, nullptr, &vulkan->mainRenderPass);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to create main render pass");
            return result;
        }
    }

    result = InternalCreatePresentationResources(vulkan);
    if (result != VK_SUCCESS) return result;

    for (int index = 0; index < 2; index++) {
        vulkan->frameStates[index].index = index;
        result = InternalCreateFrameResources(vulkan, &vulkan->frameStates[index]);
        if (result != VK_SUCCESS) return result;
    }

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
        vulkan->swapchainImageViews.clear();
        vulkan->swapchainFramebuffers.clear();

        for (VkImage image : images) {
            vulkan->swapchainImages.push_back(image);

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

            vulkan->swapchainImageViews.push_back(imageView);

            VkFramebufferCreateInfo framebufferInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = vulkan->mainRenderPass,
                .attachmentCount = 1,
                .pAttachments = &imageView,
                .width = vulkan->swapchainExtent.width,
                .height = vulkan->swapchainExtent.height,
                .layers = 1,
            };

            VkFramebuffer framebuffer;

            result = vkCreateFramebuffer(vulkan->device, &framebufferInfo, nullptr, &framebuffer);
            if (result != VK_SUCCESS) {
                Errorf(vulkan, "failed to create framebuffer");
                return result;
            }

            vulkan->swapchainFramebuffers.push_back(framebuffer);
        }
    }

    return VK_SUCCESS;
}

static void InternalDestroyPresentationResources(
    VulkanContext* vulkan)
{
    for (VkFramebuffer framebuffer : vulkan->swapchainFramebuffers)
        vkDestroyFramebuffer(vulkan->device, framebuffer, nullptr);
    vulkan->swapchainFramebuffers.clear();

    for (VkImageView imageView : vulkan->swapchainImageViews)
        vkDestroyImageView(vulkan->device, imageView, nullptr);
    vulkan->swapchainImageViews.clear();

    if (vulkan->swapchain) {
        vkDestroySwapchainKHR(vulkan->device, vulkan->swapchain, nullptr);
        vulkan->swapchain = nullptr;
        vulkan->swapchainExtent = {};
        vulkan->swapchainFormat = VK_FORMAT_UNDEFINED;
        vulkan->swapchainImages.clear();
    }
}

void DestroyVulkan(VulkanContext* vulkan)
{
    if (vulkan->device) {
        // Device exists, make sure there is nothing going on
        // before we start releasing resources.
        vkDeviceWaitIdle(vulkan->device);
    }

    for (int index = 0; index < 2; index++)
        InternalDestroyFrameResources(vulkan, &vulkan->frameStates[index]);

    // Destroy swap chain and any other window-related resources.
    InternalDestroyPresentationResources(vulkan);

    if (vulkan->mainRenderPass) {
        vkDestroyRenderPass(vulkan->device, vulkan->mainRenderPass, nullptr);
        vulkan->mainRenderPass = VK_NULL_HANDLE;
    }

    if (vulkan->descriptorPool) {
        vkDestroyDescriptorPool(vulkan->device, vulkan->descriptorPool, nullptr);
        vulkan->descriptorPool = VK_NULL_HANDLE;
    }

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

static void InternalWaitForWindowSize(
    VulkanContext* vulkan)
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(vulkan->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(vulkan->window, &width, &height);
        glfwWaitEvents();
    }
}

static VkResult InternalCreateVulkanImage(
    VulkanContext* vulkan,
    VulkanImage* image,
    VkImageUsageFlags usageFlags,
    VkMemoryPropertyFlags memoryFlags,
    VkImageType type,
    VkFormat format,
    VkExtent3D extent,
    VkImageTiling tiling)
{
    VkResult result = VK_SUCCESS;

    image->type = type;
    image->format = format;
    image->extent = extent;
    image->tiling = tiling;

    // Create the image object.
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = 0,
        .imageType = type,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usageFlags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    result = vkCreateImage(vulkan->device, &imageInfo, nullptr, &image->image);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create image");
        return result;
    }

    // Determine memory requirements for the image.
    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(vulkan->device, image->image, &memoryRequirements);

    // Find memory suitable for the image.
    uint32_t memoryTypeIndex = 0xFFFFFFFF;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(vulkan->physicalDevice, &memoryProperties);
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; index++) {
        if (!(memoryRequirements.memoryTypeBits & (1 << index)))
            continue;
        VkMemoryType& type = memoryProperties.memoryTypes[index];
        if ((type.propertyFlags & memoryFlags) != memoryFlags)
            continue;
        memoryTypeIndex = index;
        break;
    }

    // Allocate memory.
    VkMemoryAllocateInfo memoryAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    result = vkAllocateMemory(vulkan->device, &memoryAllocateInfo, nullptr, &image->memory);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to allocate image memory");
        return result;
    }

    vkBindImageMemory(vulkan->device, image->image, image->memory, 0);

    VkImageViewType viewType;

    switch (type) {
    case VK_IMAGE_TYPE_1D:
        viewType = VK_IMAGE_VIEW_TYPE_1D;
        break;
    case VK_IMAGE_TYPE_2D:
        viewType = VK_IMAGE_VIEW_TYPE_2D;
        break;
    case VK_IMAGE_TYPE_3D:
        viewType = VK_IMAGE_VIEW_TYPE_3D;
        break;
    default:
        Errorf(vulkan, "unsupported image type");
        return VK_ERROR_UNKNOWN;
    }

    // Create image view spanning the full image.
    VkImageViewCreateInfo imageViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = viewType,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    result = vkCreateImageView(vulkan->device, &imageViewInfo, nullptr, &image->view);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create image view");
        return result;
    }

    return VK_SUCCESS;
}

VulkanImage* CreateVulkanImage(
    VulkanContext* vulkan,
    VkImageUsageFlags usageFlags,
    VkMemoryPropertyFlags memoryFlags,
    VkImageType type,
    VkFormat format,
    VkExtent3D extent,
    VkImageTiling tiling)
{
    auto image = new VulkanImage;

    VkResult result = InternalCreateVulkanImage(vulkan, image, usageFlags, memoryFlags, type, format, extent, tiling);
    if (result != VK_SUCCESS) {
        DestroyVulkanImage(vulkan, image);
        image = nullptr;
    }

    return image;
}

void DestroyVulkanImage(
    VulkanContext* vulkan,
    VulkanImage* image)
{
    if (image->view)
        vkDestroyImageView(vulkan->device, image->view, nullptr);
    if (image->image)
        vkDestroyImage(vulkan->device, image->image, nullptr);
    if (image->memory)
        vkFreeMemory(vulkan->device, image->memory, nullptr);

    delete image;
}

static VkResult InternalCreateVulkanGraphicsPipeline(
    VulkanContext* vulkan,
    VulkanPipeline* pipeline,
    VulkanGraphicsPipelineConfiguration const& config)
{
    VkResult result = VK_SUCCESS;

    // Create descriptor set layout.
    for (size_t index = 0; index < config.descriptorTypes.size(); index++) {
        pipeline->descriptorSetLayoutBindings.push_back({
            .binding            = static_cast<uint32_t>(index),
            .descriptorType     = config.descriptorTypes[index],
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_ALL_GRAPHICS,
            .pImmutableSamplers = nullptr,
        });
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(pipeline->descriptorSetLayoutBindings.size()),
        .pBindings = pipeline->descriptorSetLayoutBindings.data(),
    };

    result = vkCreateDescriptorSetLayout(vulkan->device, &descriptorSetLayoutInfo, nullptr, &pipeline->descriptorSetLayout);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create descriptor set layout");
        return result;
    }

    // Create vertex shader module.
    VkShaderModuleCreateInfo vertexShaderModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = config.vertexShaderCode.size() * sizeof(uint32_t),
        .pCode = config.vertexShaderCode.data(),
    };

    VkShaderModule vertexShaderModule;
    result = vkCreateShaderModule(vulkan->device, &vertexShaderModuleInfo, nullptr, &vertexShaderModule);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create vertex shader module");
        return result;
    }

    // Create fragment shader module.
    VkShaderModuleCreateInfo fragmentShaderModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = config.fragmentShaderCode.size() * sizeof(uint32_t),
        .pCode = config.fragmentShaderCode.data(),
    };

    VkShaderModule fragmentShaderModule;
    result = vkCreateShaderModule(vulkan->device, &fragmentShaderModuleInfo, nullptr, &fragmentShaderModule);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create fragment shader module");
        return result;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShaderModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShaderModule,
            .pName = "main",
        },
    };

    // Dynamic state.
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };

    // Vertex input state.
    VkVertexInputBindingDescription vertexBindingDescription = {
        .binding = 0,
        .stride = config.vertexSize,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkPipelineVertexInputStateCreateInfo vertexInputStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = config.vertexSize > 0 ? 1u : 0u,
        .pVertexBindingDescriptions = &vertexBindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexFormat.size()),
        .pVertexAttributeDescriptions = config.vertexFormat.data(),
    };

    // Input assembler state.
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // Viewport state.
    VkPipelineViewportStateCreateInfo viewportStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    // Rasterizer state.
    VkPipelineRasterizationStateCreateInfo rasterizationStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    // Multisample state.
    VkPipelineMultisampleStateCreateInfo multisampleStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    // Depth-stencil state.
    VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    // Color blend state.
    VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachmentState,
        .blendConstants = { 0, 0, 0, 0 },
    };

    // Pipeline layout.
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &pipeline->descriptorSetLayout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };

    result = vkCreatePipelineLayout(vulkan->device, &pipelineLayoutCreateInfo, nullptr, &pipeline->pipelineLayout);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create pipeline layout");
        return result;
    }

    // Create pipeline.
    VkGraphicsPipelineCreateInfo graphicsPipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = static_cast<uint32_t>(std::size(shaderStageInfos)),
        .pStages = shaderStageInfos,
        .pVertexInputState = &vertexInputStateInfo,
        .pInputAssemblyState = &inputAssemblyStateInfo,
        .pViewportState = &viewportStateInfo,
        .pRasterizationState = &rasterizationStateInfo,
        .pMultisampleState = &multisampleStateInfo,
        .pDepthStencilState = &depthStencilStateInfo,
        .pColorBlendState = &colorBlendStateInfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = pipeline->pipelineLayout,
        .renderPass = vulkan->mainRenderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    result = vkCreateGraphicsPipelines(vulkan->device, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, nullptr, &pipeline->pipeline);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create pipeline");
        return result;
    }

    vkDestroyShaderModule(vulkan->device, vertexShaderModule, nullptr);
    vkDestroyShaderModule(vulkan->device, fragmentShaderModule, nullptr);

    // Allocate descriptor sets, one per frame state.
    for (uint32_t index = 0; index < 2; index++) {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vulkan->descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &pipeline->descriptorSetLayout,
        };

        result = vkAllocateDescriptorSets(vulkan->device, &descriptorSetAllocateInfo, &pipeline->descriptorSets[index]);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to allocate compute descriptor set");
            return result;
        }
    }

    pipeline->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    return result;
}

VulkanPipeline* CreateVulkanGraphicsPipeline(
    VulkanContext* vulkan,
    VulkanGraphicsPipelineConfiguration const& config)
{
    auto pipeline = new VulkanPipeline;

    VkResult result = InternalCreateVulkanGraphicsPipeline(vulkan, pipeline, config);
    if (result != VK_SUCCESS) {
        DestroyVulkanPipeline(vulkan, pipeline);
        pipeline = nullptr;
    }

    return pipeline;
}

static VkResult InternalCreateVulkanComputePipeline(
    VulkanContext* vulkan,
    VulkanPipeline* pipeline,
    VulkanComputePipelineConfiguration const& config)
{
    VkResult result = VK_SUCCESS;

    // Create descriptor set layout.
    for (size_t index = 0; index < config.descriptorTypes.size(); index++) {
        pipeline->descriptorSetLayoutBindings.push_back({
            .binding            = static_cast<uint32_t>(index),
            .descriptorType     = config.descriptorTypes[index],
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(pipeline->descriptorSetLayoutBindings.size()),
        .pBindings = pipeline->descriptorSetLayoutBindings.data(),
    };

    result = vkCreateDescriptorSetLayout(vulkan->device, &descriptorSetLayoutInfo, nullptr, &pipeline->descriptorSetLayout);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create descriptor set layout");
        return result;
    }

    // Create compute shader module.
    VkShaderModuleCreateInfo computeShaderModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = config.computeShaderCode.size() * sizeof(uint32_t),
        .pCode = config.computeShaderCode.data(),
    };

    VkShaderModule computeShaderModule;

    result = vkCreateShaderModule(vulkan->device, &computeShaderModuleInfo, nullptr, &computeShaderModule);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create compute shader module");
        return result;
    }

    VkPipelineShaderStageCreateInfo computeShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = computeShaderModule,
        .pName = "main",
    };

    // Create pipeline layout.
    VkPipelineLayoutCreateInfo computePipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &pipeline->descriptorSetLayout,
    };

    result = vkCreatePipelineLayout(vulkan->device, &computePipelineLayoutInfo, nullptr, &pipeline->pipelineLayout);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create compute pipeline layout");
        return result;
    }

    // Create pipeline.
    VkComputePipelineCreateInfo computePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = computeShaderStageInfo,
        .layout = pipeline->pipelineLayout,
    };

    result = vkCreateComputePipelines(vulkan->device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &pipeline->pipeline);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to create compute pipeline");
        return result;
    }

    vkDestroyShaderModule(vulkan->device, computeShaderModule, nullptr);

    // Allocate descriptor sets, one per frame state.
    for (uint32_t index = 0; index < 2; index++) {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vulkan->descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &pipeline->descriptorSetLayout,
        };

        result = vkAllocateDescriptorSets(vulkan->device, &descriptorSetAllocateInfo, &pipeline->descriptorSets[index]);
        if (result != VK_SUCCESS) {
            Errorf(vulkan, "failed to allocate compute descriptor set");
            return result;
        }
    }

    pipeline->bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    return result;
}

VulkanPipeline* CreateVulkanComputePipeline(
    VulkanContext* vulkan,
    VulkanComputePipelineConfiguration const& config)
{
    auto pipeline = new VulkanPipeline;

    VkResult result = InternalCreateVulkanComputePipeline(vulkan, pipeline, config);
    if (result != VK_SUCCESS) {
        DestroyVulkanPipeline(vulkan, pipeline);
        pipeline = nullptr;
    }

    return pipeline;
}

void DestroyVulkanPipeline(
    VulkanContext* vulkan,
    VulkanPipeline* pipeline)
{
    if (pipeline->pipeline)
        vkDestroyPipeline(vulkan->device, pipeline->pipeline, nullptr);
    if (pipeline->pipelineLayout)
        vkDestroyPipelineLayout(vulkan->device, pipeline->pipelineLayout, nullptr);
    if (pipeline->descriptorSetLayout)
        vkDestroyDescriptorSetLayout(vulkan->device, pipeline->descriptorSetLayout, nullptr);
}

VkResult BeginFrame(
    VulkanContext* vulkan,
    VulkanFrameState** frameOut)
{
    VkResult result = VK_SUCCESS;

    int frameIndex = (vulkan->frameIndex + 1) % 2;
    VulkanFrameState* frame = &vulkan->frameStates[frameIndex];

    // Wait for the previous commands using this frame state to finish executing.
    vkWaitForFences(vulkan->device, 1, &frame->availableFence, VK_TRUE, UINT64_MAX);

    // Try to acquire a swap chain image for us to render to.
    result = vkAcquireNextImageKHR(vulkan->device, vulkan->swapchain, UINT64_MAX, frame->imageAvailableSemaphore, VK_NULL_HANDLE, &frame->imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        InternalWaitForWindowSize(vulkan);
        vkDeviceWaitIdle(vulkan->device);
        InternalDestroyPresentationResources(vulkan);
        InternalCreatePresentationResources(vulkan);
        result = vkAcquireNextImageKHR(vulkan->device, vulkan->swapchain, UINT64_MAX, frame->imageAvailableSemaphore, VK_NULL_HANDLE, &frame->imageIndex);
    }

    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to acquire swap chain image");
        return result;
    }

    // Reset the fence to indicate that the frame state is no longer available.
    vkResetFences(vulkan->device, 1, &frame->availableFence);

    // Start compute command buffer.
    vkResetCommandBuffer(frame->computeCommandBuffer, 0);
    auto computeBeginInfo = VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };
    result = vkBeginCommandBuffer(frame->computeCommandBuffer, &computeBeginInfo);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to begin recording compute command buffer");
        return result;
    }

    // Start graphics command buffer.
    vkResetCommandBuffer(frame->graphicsCommandBuffer, 0);
    auto graphicsBeginInfo = VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };
    result = vkBeginCommandBuffer(frame->graphicsCommandBuffer, &graphicsBeginInfo);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to begin recording graphics command buffer");
        return result;
    }

    VkClearValue clearValues[] = {
        { .color = {{ 0.0f, 0.0f, 0.0f, 1.0f }} },
        { .depthStencil = { 1.0f, 0 } }
    };

    VkRenderPassBeginInfo renderPassBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vulkan->mainRenderPass,
        .framebuffer = vulkan->swapchainFramebuffers[frame->imageIndex],
        .renderArea = { .offset = { 0, 0 }, .extent = vulkan->swapchainExtent },
        .clearValueCount = 2,
        .pClearValues = clearValues,
    };

    vkCmdBeginRenderPass(frame->graphicsCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vulkan->frameIndex = frameIndex;
    *frameOut = frame;

    return VK_SUCCESS;
}

VkResult EndFrame(
    VulkanContext* vulkan,
    VulkanFrameState* frame)
{
    VkResult result = VK_SUCCESS;

    vkCmdEndRenderPass(frame->graphicsCommandBuffer);

    result = vkEndCommandBuffer(frame->graphicsCommandBuffer);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to end recording graphics command buffer");
        return result;
    }

    result = vkEndCommandBuffer(frame->computeCommandBuffer);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to end recording compute command buffer");
        return result;
    }

    VkPipelineStageFlags graphicsWaitStages[] = {
        //VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkSemaphore graphicsWaitSemaphores[] = {
        //frame->computeFinishedSemaphore,
        frame->imageAvailableSemaphore,
    };

    auto graphicsSubmitInfo = VkSubmitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = graphicsWaitSemaphores,
        .pWaitDstStageMask = graphicsWaitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame->graphicsCommandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &frame->imageFinishedSemaphore,
    };

    result = vkQueueSubmit(vulkan->graphicsQueue, 1, &graphicsSubmitInfo, frame->availableFence);
    if (result != VK_SUCCESS) {
        Errorf(vulkan, "failed to submit graphics command buffer");
        return result;
    }

    auto presentInfo = VkPresentInfoKHR {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->imageFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &vulkan->swapchain,
        .pImageIndices = &frame->imageIndex,
        .pResults = nullptr,
    };

    result = vkQueuePresentKHR(vulkan->presentQueue, &presentInfo);
    if (result != VK_SUCCESS) {
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
            Errorf(vulkan, "failed to present swap chain image");
        return result;
    }

    return VK_SUCCESS;
}

void UpdateVulkanPipelineDescriptors(
    VulkanContext* vulkan,
    VulkanFrameState* frame,
    VulkanPipeline* pipeline,
    std::span<VulkanDescriptor> descriptors)
{
    if (pipeline->descriptorSetLayoutBindings.size() != descriptors.size()) {
        Errorf(vulkan, "number of descriptors specified in update do not match the layout");
        return;
    }

    std::vector<VkWriteDescriptorSet> writes;

    for (size_t index = 0; index < descriptors.size(); index++) {
        VkDescriptorSetLayoutBinding& binding = pipeline->descriptorSetLayoutBindings[index];
        VulkanDescriptor const& descriptor = descriptors[index];

        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = pipeline->descriptorSets[frame->index],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = binding.descriptorType,
        };

        switch (binding.descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            write.pBufferInfo = &descriptor.buffer;
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            write.pImageInfo = &descriptor.image;
            break;
        }

        writes.push_back(write);
    }

    vkUpdateDescriptorSets(vulkan->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void BindVulkanPipeline(
    VulkanContext* vulkan,
    VulkanFrameState* frame,
    VulkanPipeline* pipeline)
{
    switch (pipeline->bindPoint) {
        case VK_PIPELINE_BIND_POINT_GRAPHICS: {
            vkCmdBindPipeline(frame->graphicsCommandBuffer, pipeline->bindPoint, pipeline->pipeline);
            vkCmdBindDescriptorSets(frame->graphicsCommandBuffer, pipeline->bindPoint, pipeline->pipelineLayout, 0, 1, &pipeline->descriptorSets[frame->index], 0, nullptr);

            VkViewport viewport = {
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(vulkan->swapchainExtent.width),
                .height = static_cast<float>(vulkan->swapchainExtent.height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            };
            vkCmdSetViewport(frame->graphicsCommandBuffer, 0, 1, &viewport);

            VkRect2D scissor = {
                .offset = { 0, 0 },
                .extent = vulkan->swapchainExtent,
            };
            vkCmdSetScissor(frame->graphicsCommandBuffer, 0, 1, &scissor);

            break;
        }
        case VK_PIPELINE_BIND_POINT_COMPUTE: {
            vkCmdBindPipeline(frame->computeCommandBuffer, pipeline->bindPoint, pipeline->pipeline);
            vkCmdBindDescriptorSets(frame->computeCommandBuffer, pipeline->bindPoint, pipeline->pipelineLayout, 0, 1, &pipeline->descriptorSets[frame->index], 0, nullptr);
            break;
        }
    }
}
