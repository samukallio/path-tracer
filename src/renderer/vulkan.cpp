#include "renderer/vulkan.h"
#include "scene/scene.h"
#include "application/ui_font.h"

#include <array>
#include <cassert>
#include <cstdarg>
#include <vector>
#include <set>
#include <algorithm>
#include <optional>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

uint32_t const RENDER_WIDTH = 1920;
uint32_t const RENDER_HEIGHT = 1080;

uint32_t const RESOLVE_VERTEX_SHADER[] =
{
    #include "resolve.vertex.inc"
};

uint32_t const RESOLVE_FRAGMENT_SHADER[] =
{
    #include "resolve.fragment.inc"
};

uint32_t const RENDER_COMPUTE_SHADER[] =
{
    #include "render.compute.inc"
};

uint32_t const IMGUI_VERTEX_SHADER[] =
{
    #include "imgui.vertex.inc"
};

uint32_t const IMGUI_FRAGMENT_SHADER[] =
{
    #include "imgui.fragment.inc"
};

struct imgui_push_constant_buffer
{
    uint TextureID;
};

static void Errorf(vulkan_context* Vk, char const* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    vfprintf(stderr, Fmt, Args);
    fprintf(stderr, "\n");
    va_end(Args);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT MessageType,
    const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
    void* UserDataPtr)
{
    auto Context = static_cast<vulkan_context*>(UserDataPtr);
    Errorf(Context, CallbackData->pMessage);
    return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance Instance,
    const VkDebugUtilsMessengerCreateInfoEXT* CreateInfo,
    const VkAllocationCallbacks* Allocator,
    VkDebugUtilsMessengerEXT* DebugMessengerOut)
{
    auto Fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT");
    if (!Fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return Fn(Instance, CreateInfo, Allocator, DebugMessengerOut);
}

static void DestroyDebugUtilsMessengerEXT(
    VkInstance Instance,
    VkDebugUtilsMessengerEXT DebugMessenger,
    const VkAllocationCallbacks* Allocator)
{
    auto Fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT");
    if (!Fn) return;
    Fn(Instance, DebugMessenger, Allocator);
}

static VkResult InternalCreateBuffer(
    vulkan_context* Vulkan,
    vulkan_buffer* Buffer,
    VkBufferUsageFlags UsageFlags,
    VkMemoryPropertyFlags MemoryFlags,
    VkDeviceSize Size)
{
    VkResult Result = VK_SUCCESS;

    Buffer->Size = Size;

    // Create the buffer.
    VkBufferCreateInfo BufferInfo = {
        .sType          = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size           = Size,
        .usage          = UsageFlags,
        .sharingMode    = VK_SHARING_MODE_EXCLUSIVE,
    };

    Result = vkCreateBuffer(Vulkan->Device, &BufferInfo, nullptr, &Buffer->Buffer);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create buffer");
        return Result;
    }

    // Determine memory requirements for the buffer.
    VkMemoryRequirements MemoryRequirements;
    vkGetBufferMemoryRequirements(Vulkan->Device, Buffer->Buffer, &MemoryRequirements);

    // Find memory suitable for the image.
    uint32_t MemoryTypeIndex = 0xFFFFFFFF;
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(Vulkan->PhysicalDevice, &MemoryProperties);
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; Index++) {
        if (!(MemoryRequirements.memoryTypeBits & (1 << Index)))
            continue;
        VkMemoryType& Type = MemoryProperties.memoryTypes[Index];
        if ((Type.propertyFlags & MemoryFlags) != MemoryFlags)
            continue;
        MemoryTypeIndex = Index;
        break;
    }

    // Allocate memory.
    VkMemoryAllocateInfo MemoryAllocateInfo = {
        .sType              = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize     = MemoryRequirements.size,
        .memoryTypeIndex    = MemoryTypeIndex,
    };

    Result = vkAllocateMemory(Vulkan->Device, &MemoryAllocateInfo, nullptr, &Buffer->Memory);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to allocate image memory");
        return Result;
    }

    vkBindBufferMemory(Vulkan->Device, Buffer->Buffer, Buffer->Memory, 0);

    return VK_SUCCESS;
}

static void InternalDestroyBuffer(
    vulkan_context* Vulkan,
    vulkan_buffer* Buffer)
{
    if (Buffer->Buffer)
        vkDestroyBuffer(Vulkan->Device, Buffer->Buffer, nullptr);
    if (Buffer->Memory)
        vkFreeMemory(Vulkan->Device, Buffer->Memory, nullptr);
}

static void InternalWriteToHostVisibleBuffer(
    vulkan_context* Vulkan,
    vulkan_buffer* Buffer,
    void const* Data,
    size_t Size)
{
    assert(Size <= Buffer->Size);
    void* BufferMemory;
    vkMapMemory(Vulkan->Device, Buffer->Memory, 0, Buffer->Size, 0, &BufferMemory);
    memcpy(BufferMemory, Data, Size);
    vkUnmapMemory(Vulkan->Device, Buffer->Memory);
}

static void InternalWriteToDeviceLocalBuffer(
    vulkan_context* Vulkan,
    vulkan_buffer* Buffer,
    void const* Data,
    size_t Size)
{
    if (Size == 0) return;

    // Create a staging buffer and copy the data into it.
    vulkan_buffer Staging;
    InternalCreateBuffer(Vulkan,
        &Staging,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        Buffer->Size);
    InternalWriteToHostVisibleBuffer(Vulkan, &Staging, Data, Size);

    // Now copy the data into the device local buffer.
    VkCommandBufferAllocateInfo AllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = Vulkan->ComputeCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer CommandBuffer;
    vkAllocateCommandBuffers(Vulkan->Device, &AllocateInfo, &CommandBuffer);

    VkCommandBufferBeginInfo BeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(CommandBuffer, &BeginInfo);

    VkBufferCopy Region = {
        .srcOffset  = 0,
        .dstOffset  = 0,
        .size       = Buffer->Size,
    };
    vkCmdCopyBuffer(CommandBuffer, Staging.Buffer, Buffer->Buffer, 1, &Region);

    vkEndCommandBuffer(CommandBuffer);

    VkSubmitInfo SubmitInfo = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &CommandBuffer,
    };
    vkQueueSubmit(Vulkan->ComputeQueue, 1, &SubmitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(Vulkan->ComputeQueue);

    vkFreeCommandBuffers(Vulkan->Device, Vulkan->ComputeCommandPool, 1, &CommandBuffer);

    // Delete the staging buffer.
    InternalDestroyBuffer(Vulkan, &Staging);
}

static VkResult InternalCreateImage(
    vulkan_context*         Vulkan,
    vulkan_image*           Image,
    VkImageUsageFlags       UsageFlags,
    VkMemoryPropertyFlags   MemoryFlags,
    VkImageType             Type,
    VkFormat                Format,
    VkExtent3D              Extent,
    uint32_t                LayerCount,
    VkImageTiling           Tiling,
    VkImageLayout           Layout,
    bool                    Compute)
{
    VkResult Result = VK_SUCCESS;

    Image->Type = Type;
    Image->Format = Format;
    Image->Extent = Extent;
    Image->Tiling = Tiling;
    Image->LayerCount = std::max(1u, LayerCount);

    // Create the image object.
    VkImageCreateInfo ImageInfo = {
        .sType          = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags          = 0,
        .imageType      = Type,
        .format         = Format,
        .extent         = Extent,
        .mipLevels      = 1,
        .arrayLayers    = Image->LayerCount,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .tiling         = Tiling,
        .usage          = UsageFlags,
        .sharingMode    = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    Result = vkCreateImage(Vulkan->Device, &ImageInfo, nullptr, &Image->Image);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create image");
        return Result;
    }

    // Determine memory requirements for the image.
    VkMemoryRequirements MemoryRequirements;
    vkGetImageMemoryRequirements(Vulkan->Device, Image->Image, &MemoryRequirements);

    // Find memory suitable for the image.
    uint32_t MemoryTypeIndex = 0xFFFFFFFF;
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(Vulkan->PhysicalDevice, &MemoryProperties);
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; Index++) {
        if (!(MemoryRequirements.memoryTypeBits & (1 << Index)))
            continue;
        VkMemoryType& Type = MemoryProperties.memoryTypes[Index];
        if ((Type.propertyFlags & MemoryFlags) != MemoryFlags)
            continue;
        MemoryTypeIndex = Index;
        break;
    }

    // Allocate memory.
    VkMemoryAllocateInfo MemoryAllocateInfo = {
        .sType              = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize     = MemoryRequirements.size,
        .memoryTypeIndex    = MemoryTypeIndex,
    };

    Result = vkAllocateMemory(Vulkan->Device, &MemoryAllocateInfo, nullptr, &Image->Memory);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to allocate image memory");
        return Result;
    }

    vkBindImageMemory(Vulkan->Device, Image->Image, Image->Memory, 0);

    VkImageViewType ViewType;

    switch (Type) {
    case VK_IMAGE_TYPE_1D:
        ViewType = LayerCount > 0 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        break;
    case VK_IMAGE_TYPE_2D:
        ViewType = LayerCount > 0 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        break;
    case VK_IMAGE_TYPE_3D:
        ViewType = VK_IMAGE_VIEW_TYPE_3D;
        break;
    default:
        Errorf(Vulkan, "unsupported image type");
        return VK_ERROR_UNKNOWN;
    }

    // Create image view spanning the full image.
    VkImageViewCreateInfo ImageViewInfo = {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image      = Image->Image,
        .viewType   = ViewType,
        .format     = Format,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = Image->LayerCount,
        },
    };

    Result = vkCreateImageView(Vulkan->Device, &ImageViewInfo, nullptr, &Image->View);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create image view");
        return Result;
    }

    if (Layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        VkCommandPool CommandPool = Compute
            ? Vulkan->ComputeCommandPool
            : Vulkan->GraphicsCommandPool;

        VkQueue Queue = Compute
            ? Vulkan->ComputeQueue
            : Vulkan->GraphicsQueue;

        VkCommandBufferAllocateInfo AllocateInfo = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = CommandPool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer CommandBuffer;
        vkAllocateCommandBuffers(Vulkan->Device, &AllocateInfo, &CommandBuffer);

        VkCommandBufferBeginInfo BeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(CommandBuffer, &BeginInfo);

        VkImageMemoryBarrier Barrier = {
            .sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask          = 0,
            .dstAccessMask          = 0,
            .oldLayout              = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout              = Layout,
            .srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
            .image                  = Image->Image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = Image->LayerCount,
            },
        };

        vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &Barrier);

        vkEndCommandBuffer(CommandBuffer);

        VkSubmitInfo submitInfo = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &CommandBuffer,
        };

        vkQueueSubmit(Vulkan->GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Queue);
        vkFreeCommandBuffers(Vulkan->Device, CommandPool, 1, &CommandBuffer);
    }

    return Result;
}

static void InternalDestroyImage(
    vulkan_context* Vulkan,
    vulkan_image*   Image)
{
    if (Image->View)
        vkDestroyImageView(Vulkan->Device, Image->View, nullptr);
    if (Image->Image)
        vkDestroyImage(Vulkan->Device, Image->Image, nullptr);
    if (Image->Memory)
        vkFreeMemory(Vulkan->Device, Image->Memory, nullptr);
}

static VkResult InternalWriteToDeviceLocalImage(
    vulkan_context* Vulkan,
    vulkan_image*   Image,
    uint32_t        LayerIndex,
    uint32_t        LayerCount,
    void const*     Data,
    uint32_t        Width,
    uint32_t        Height,
    uint32_t        BytesPerPixel,
    VkImageLayout   NewLayout)
{
    size_t Size = Width * Height * BytesPerPixel;

    // Create a staging buffer and copy the data into it.
    vulkan_buffer Staging;
    InternalCreateBuffer(Vulkan,
        &Staging,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        Size);
    InternalWriteToHostVisibleBuffer(Vulkan, &Staging, Data, Size);

    VkCommandBufferAllocateInfo AllocateInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = Vulkan->ComputeCommandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer CommandBuffer;
    vkAllocateCommandBuffers(Vulkan->Device, &AllocateInfo, &CommandBuffer);

    VkCommandBufferBeginInfo BeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(CommandBuffer, &BeginInfo);

    VkBufferImageCopy Region = {
        .bufferOffset       = 0,
        .bufferRowLength    = Width,
        .bufferImageHeight  = Height,
        .imageSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = LayerIndex,
            .layerCount     = LayerCount,
        },
        .imageOffset        = { 0, 0 },
        .imageExtent        = { Width, Height, 1 },
    };

    vkCmdCopyBufferToImage(CommandBuffer,
        Staging.Buffer, Image->Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &Region);

    VkImageMemoryBarrier Barrier = {
        .sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
        .image                  = Image->Image,
        .subresourceRange =  {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = LayerIndex,
            .layerCount     = LayerCount,
        },
    };

    //int32_t srcWidth = image->extent.width;
    //int32_t srcHeight = image->extent.height;

    //for (uint32_t level = 1; level < image->levelCount; level++) {
    //    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    //    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    //    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    //    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    //    barrier.subresourceRange.baseMipLevel = level - 1;

    //    vkCmdPipelineBarrier(commandBuffer,
    //        VK_PIPELINE_STAGE_TRANSFER_BIT,
    //        VK_PIPELINE_STAGE_TRANSFER_BIT,
    //        0,
    //        0, nullptr,
    //        0, nullptr,
    //        1, &barrier);

    //    int32_t dstWidth = std::max(1, srcWidth / 2);
    //    int32_t dstHeight = std::max(1, srcHeight / 2);

    //    VkImageBlit blit =
    //    {
    //        .srcSubresource =
    //        {
    //            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    //            .mipLevel = level - 1,
    //            .baseArrayLayer = 0,
    //            .layerCount = 1,
    //        },
    //        .srcOffsets =
    //        {
    //            { 0, 0, 0 },
    //            { srcWidth, srcHeight, 1 },
    //        },
    //        .dstSubresource =
    //        {
    //            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    //            .mipLevel = level,
    //            .baseArrayLayer = 0,
    //            .layerCount = 1,
    //        },
    //        .dstOffsets =
    //        {
    //            { 0, 0, 0 },
    //            { dstWidth, dstHeight, 1 },
    //        },
    //    };

    //    vkCmdBlitImage(commandBuffer,
    //        vki->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    //        vki->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //        1, &blit, VK_FILTER_LINEAR);

    //    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    //    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    //    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    //    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    //    vkCmdPipelineBarrier(commandBuffer,
    //        VK_PIPELINE_STAGE_TRANSFER_BIT,
    //        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    //        0,
    //        0, nullptr,
    //        0, nullptr,
    //        1, &barrier);

    //    srcWidth = dstWidth;
    //    srcHeight = dstHeight;
    //}

    Barrier.subresourceRange.baseMipLevel = 0; //vki->levelCount - 1;
    Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    Barrier.dstAccessMask = 0;
    Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    Barrier.newLayout = NewLayout;
    vkCmdPipelineBarrier(CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &Barrier);

    vkEndCommandBuffer(CommandBuffer);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &CommandBuffer,
    };
    vkQueueSubmit(Vulkan->ComputeQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(Vulkan->ComputeQueue);

    vkFreeCommandBuffers(Vulkan->Device, Vulkan->ComputeCommandPool, 1, &CommandBuffer);

    // Delete the staging buffer.
    InternalDestroyBuffer(Vulkan, &Staging);

    return VK_SUCCESS;
}


static VkResult InternalCreatePresentationResources(
    vulkan_context* Vulkan)
{
    VkResult Result = VK_SUCCESS;

    // Create the swap chain.
    {
        // Determine current window surface capabilities.
        VkSurfaceCapabilitiesKHR SurfaceCapabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Vulkan->PhysicalDevice, Vulkan->Surface, &SurfaceCapabilities);

        // Determine width and height of the swap chain.
        VkExtent2D ImageExtent = SurfaceCapabilities.currentExtent;
        if (ImageExtent.width == 0xFFFFFFFF) {
            int Width, Height;
            glfwGetFramebufferSize(Vulkan->Window, &Width, &Height);
            ImageExtent.width = std::clamp(
                static_cast<uint32_t>(Width),
                SurfaceCapabilities.minImageExtent.width,
                SurfaceCapabilities.maxImageExtent.width);
            ImageExtent.height = std::clamp(
                static_cast<uint32_t>(Height),
                SurfaceCapabilities.minImageExtent.height,
                SurfaceCapabilities.maxImageExtent.height);
        }

        // Determine swap chain image count.
        uint32_t ImageCount = SurfaceCapabilities.minImageCount + 1;
        if (SurfaceCapabilities.maxImageCount > 0)
            ImageCount = std::min(ImageCount, SurfaceCapabilities.maxImageCount);

        auto SwapChainInfo = VkSwapchainCreateInfoKHR {
            .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface            = Vulkan->Surface,
            .minImageCount      = ImageCount,
            .imageFormat        = Vulkan->SurfaceFormat.format,
            .imageColorSpace    = Vulkan->SurfaceFormat.colorSpace,
            .imageExtent        = ImageExtent,
            .imageArrayLayers   = 1,
            .imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform       = SurfaceCapabilities.currentTransform,
            .compositeAlpha     = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode        = Vulkan->PresentMode,
            .clipped            = VK_TRUE,
            .oldSwapchain       = VK_NULL_HANDLE,
        };

        if (Vulkan->GraphicsQueueFamilyIndex == Vulkan->PresentQueueFamilyIndex) {
            SwapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            SwapChainInfo.queueFamilyIndexCount = 0;
            SwapChainInfo.pQueueFamilyIndices = nullptr;
        }
        else {
            uint32_t const QueueFamilyIndices[] = {
                Vulkan->GraphicsQueueFamilyIndex,
                Vulkan->PresentQueueFamilyIndex
            };
            SwapChainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            SwapChainInfo.queueFamilyIndexCount = 2;
            SwapChainInfo.pQueueFamilyIndices = QueueFamilyIndices;
        }

        Result = vkCreateSwapchainKHR(Vulkan->Device, &SwapChainInfo, nullptr, &Vulkan->SwapChain);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create swap chain");
            return Result;
        }

        Vulkan->SwapChainExtent = ImageExtent;
        Vulkan->SwapChainFormat = SwapChainInfo.imageFormat;
    }

    // Retrieve swap chain images.
    {
        uint32_t ImageCount = 0;
        vkGetSwapchainImagesKHR(Vulkan->Device, Vulkan->SwapChain, &ImageCount, nullptr);
        auto Images = std::vector<VkImage>(ImageCount);
        vkGetSwapchainImagesKHR(Vulkan->Device, Vulkan->SwapChain, &ImageCount, Images.data());

        Vulkan->SwapChainImages.clear();
        Vulkan->SwapChainImageViews.clear();
        Vulkan->SwapChainFrameBuffers.clear();

        for (VkImage Image : Images) {
            Vulkan->SwapChainImages.push_back(Image);

            auto ImageViewInfo = VkImageViewCreateInfo {
                .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image      = Image,
                .viewType   = VK_IMAGE_VIEW_TYPE_2D,
                .format     = Vulkan->SwapChainFormat,
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                .subresourceRange = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                }
            };

            VkImageView ImageView;

            Result = vkCreateImageView(Vulkan->Device, &ImageViewInfo, nullptr, &ImageView);
            if (Result != VK_SUCCESS) {
                Errorf(Vulkan, "failed to create image view");
                return Result;
            }

            Vulkan->SwapChainImageViews.push_back(ImageView);

            auto FrameBufferInfo = VkFramebufferCreateInfo {
                .sType              = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass         = Vulkan->MainRenderPass,
                .attachmentCount    = 1,
                .pAttachments       = &ImageView,
                .width              = Vulkan->SwapChainExtent.width,
                .height             = Vulkan->SwapChainExtent.height,
                .layers             = 1,
            };

            VkFramebuffer FrameBuffer;

            Result = vkCreateFramebuffer(Vulkan->Device, &FrameBufferInfo, nullptr, &FrameBuffer);
            if (Result != VK_SUCCESS) {
                Errorf(Vulkan, "failed to create framebuffer");
                return Result;
            }

            Vulkan->SwapChainFrameBuffers.push_back(FrameBuffer);
        }
    }

    return VK_SUCCESS;
}

static void InternalDestroyPresentationResources(
    vulkan_context* Vulkan)
{
    for (VkFramebuffer FrameBuffer : Vulkan->SwapChainFrameBuffers)
        vkDestroyFramebuffer(Vulkan->Device, FrameBuffer, nullptr);
    Vulkan->SwapChainFrameBuffers.clear();

    for (VkImageView ImageView : Vulkan->SwapChainImageViews)
        vkDestroyImageView(Vulkan->Device, ImageView, nullptr);
    Vulkan->SwapChainImageViews.clear();

    if (Vulkan->SwapChain) {
        vkDestroySwapchainKHR(Vulkan->Device, Vulkan->SwapChain, nullptr);
        Vulkan->SwapChain = nullptr;
        Vulkan->SwapChainExtent = {};
        Vulkan->SwapChainFormat = VK_FORMAT_UNDEFINED;
        Vulkan->SwapChainImages.clear();
    }
}

static VkResult InternalCreateFrameResources(
    vulkan_context* Vulkan)
{
    VkResult Result = VK_SUCCESS;

    for (int Index = 0; Index < 2; Index++) {
        vulkan_frame_state* Frame = &Vulkan->FrameStates[Index];

        Frame->Index = Index;
        Frame->Fresh = true;

        auto GraphicsCommandBufferAllocateInfo = VkCommandBufferAllocateInfo {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = Vulkan->GraphicsCommandPool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        Result = vkAllocateCommandBuffers(Vulkan->Device, &GraphicsCommandBufferAllocateInfo, &Frame->GraphicsCommandBuffer);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to allocate graphics command buffer");
            return Result;
        }

        auto ComputeCommandBufferAllocateInfo = VkCommandBufferAllocateInfo {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = Vulkan->ComputeCommandPool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        Result = vkAllocateCommandBuffers(Vulkan->Device, &ComputeCommandBufferAllocateInfo, &Frame->ComputeCommandBuffer);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to allocate compute command buffer");
            return Result;
        }

        auto SemaphoreInfo = VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };

        VkSemaphore* SemaphorePtrs[] = {
            &Frame->ImageAvailableSemaphore,
            &Frame->ImageFinishedSemaphore,
            &Frame->ComputeToComputeSemaphore,
            &Frame->ComputeToGraphicsSemaphore,
        };

        for (VkSemaphore* SemaphorePtr : SemaphorePtrs) {
            Result = vkCreateSemaphore(Vulkan->Device, &SemaphoreInfo, nullptr, SemaphorePtr);
            if (Result != VK_SUCCESS) {
                Errorf(Vulkan, "failed to create semaphore");
                return Result;
            }
        }

        auto FenceInfo = VkFenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        VkFence* FencePtrs[] = {
            &Frame->AvailableFence,
        };

        for (VkFence* FencePtr : FencePtrs) {
            Result = vkCreateFence(Vulkan->Device, &FenceInfo, nullptr, FencePtr);
            if (Result != VK_SUCCESS) {
                Errorf(Vulkan, "failed to create semaphore");
                return Result;
            }
        }

        InternalCreateBuffer(Vulkan,
            &Frame->FrameUniformBuffer,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(frame_uniform_buffer));

        InternalCreateImage(Vulkan,
            &Frame->RenderTarget,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            { .width = RENDER_WIDTH, .height = RENDER_HEIGHT, .depth = 1 },
            0,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            true);

        InternalCreateImage(Vulkan,
            &Frame->RenderTargetGraphicsCopy,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            { .width = RENDER_WIDTH, .height = RENDER_HEIGHT, .depth = 1 },
            0,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            false);

        InternalCreateBuffer(Vulkan,
            &Frame->ImguiUniformBuffer,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(imgui_uniform_buffer));

        InternalCreateBuffer(Vulkan,
            &Frame->ImguiVertexBuffer,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            65536 * sizeof(ImDrawVert));

        InternalCreateBuffer(Vulkan,
            &Frame->ImguiIndexBuffer,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            65536 * sizeof(uint16_t));
    }

    // Allocate and initialize descriptor sets.
    for (int Index = 0; Index < 2; Index++) {
        vulkan_frame_state* Frame0 = &Vulkan->FrameStates[1-Index];
        vulkan_frame_state* Frame = &Vulkan->FrameStates[Index];

        // Render descriptor set.
        {
            VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo = {
                .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool     = Vulkan->DescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts        = &Vulkan->RenderPipeline.DescriptorSetLayout,
            };

            Result = vkAllocateDescriptorSets(Vulkan->Device, &DescriptorSetAllocateInfo, &Frame->RenderDescriptorSet);
            if (Result != VK_SUCCESS) {
                Errorf(Vulkan, "failed to allocate compute descriptor set");
                return Result;
            }

            auto FrameUniformBufferInfo = VkDescriptorBufferInfo {
                .buffer = Frame->FrameUniformBuffer.Buffer,
                .offset = 0,
                .range  = Frame->FrameUniformBuffer.Size,
            };

            auto SrcImageInfo = VkDescriptorImageInfo {
                .sampler     = nullptr,
                .imageView   = Frame0->RenderTarget.View,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };

            auto DstImageInfo = VkDescriptorImageInfo {
                .sampler     = nullptr,
                .imageView   = Frame->RenderTarget.View,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };

            VkWriteDescriptorSet Writes[] = {
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = Frame->RenderDescriptorSet,
                    .dstBinding      = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo     = &FrameUniformBufferInfo,
                },
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = Frame->RenderDescriptorSet,
                    .dstBinding      = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo      = &SrcImageInfo,
                },
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = Frame->RenderDescriptorSet,
                    .dstBinding      = 2,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo      = &DstImageInfo,
                }
            };

            vkUpdateDescriptorSets(Vulkan->Device, static_cast<uint32_t>(std::size(Writes)), Writes, 0, nullptr);
        }

        // Resolve descriptor set.
        {
            auto DescriptorSetAllocateInfo = VkDescriptorSetAllocateInfo {
                .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool     = Vulkan->DescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts        = &Vulkan->ResolvePipeline.DescriptorSetLayout,
            };

            Result = vkAllocateDescriptorSets(Vulkan->Device, &DescriptorSetAllocateInfo, &Frame->ResolveDescriptorSet);
            if (Result != VK_SUCCESS) {
                Errorf(Vulkan, "failed to allocate graphics descriptor set");
                return Result;
            }

            auto FrameUniformBufferInfo = VkDescriptorBufferInfo {
                .buffer = Frame->FrameUniformBuffer.Buffer,
                .offset = 0,
                .range  = Frame->FrameUniformBuffer.Size,
            };

            auto SrcImageInfo = VkDescriptorImageInfo {
                .sampler     = Vulkan->ImageSamplerLinear,
                .imageView   = Frame->RenderTargetGraphicsCopy.View,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkWriteDescriptorSet Writes[] = {
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = Frame->ResolveDescriptorSet,
                    .dstBinding      = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo     = &FrameUniformBufferInfo,
                },
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = Frame->ResolveDescriptorSet,
                    .dstBinding      = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo      = &SrcImageInfo,
                },
            };

            vkUpdateDescriptorSets(Vulkan->Device, static_cast<uint32_t>(std::size(Writes)), Writes, 0, nullptr);
        }

        // ImGui descriptor set.
        {
            auto DescriptorSetAllocateInfo = VkDescriptorSetAllocateInfo {
                .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool     = Vulkan->DescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts        = &Vulkan->ImguiPipeline.DescriptorSetLayout,
            };

            Result = vkAllocateDescriptorSets(Vulkan->Device, &DescriptorSetAllocateInfo, &Frame->ImguiDescriptorSet);
            if (Result != VK_SUCCESS) {
                Errorf(Vulkan, "failed to allocate imgui descriptor set");
                return Result;
            }

            auto ImguiUniformBufferInfo = VkDescriptorBufferInfo {
                .buffer = Frame->ImguiUniformBuffer.Buffer,
                .offset = 0,
                .range  = Frame->ImguiUniformBuffer.Size,
            };

            auto ImguiTextureInfo = VkDescriptorImageInfo {
                .sampler     = Vulkan->ImageSamplerLinear,
                .imageView   = Vulkan->ImguiTexture.View,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkWriteDescriptorSet Writes[] = {
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = Frame->ImguiDescriptorSet,
                    .dstBinding      = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo     = &ImguiUniformBufferInfo,
                },
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = Frame->ImguiDescriptorSet,
                    .dstBinding      = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo      = &ImguiTextureInfo,
                },
            };

            vkUpdateDescriptorSets(Vulkan->Device, static_cast<uint32_t>(std::size(Writes)), Writes, 0, nullptr);
        }
    }

    return VK_SUCCESS;
}

static VkResult InternalDestroyFrameResources(
    vulkan_context* Vulkan)
{
    for (int Index = 0; Index < 2; Index++) {
        vulkan_frame_state* Frame = &Vulkan->FrameStates[Index];

        InternalDestroyBuffer(Vulkan, &Frame->ImguiIndexBuffer);
        InternalDestroyBuffer(Vulkan, &Frame->ImguiVertexBuffer);
        InternalDestroyBuffer(Vulkan, &Frame->ImguiUniformBuffer);

        InternalDestroyImage(Vulkan, &Frame->RenderTargetGraphicsCopy);
        InternalDestroyImage(Vulkan, &Frame->RenderTarget);
        InternalDestroyBuffer(Vulkan, &Frame->FrameUniformBuffer);

        vkDestroySemaphore(Vulkan->Device, Frame->ComputeToComputeSemaphore, nullptr);
        vkDestroySemaphore(Vulkan->Device, Frame->ComputeToGraphicsSemaphore, nullptr);
        vkDestroySemaphore(Vulkan->Device, Frame->ImageAvailableSemaphore, nullptr);
        vkDestroySemaphore(Vulkan->Device, Frame->ImageFinishedSemaphore, nullptr);
        vkDestroyFence(Vulkan->Device, Frame->AvailableFence, nullptr);
    }

    return VK_SUCCESS;
}

struct vulkan_graphics_pipeline_configuration
{
    using vertex_format = std::vector<VkVertexInputAttributeDescription>;
    using descriptor_types = std::vector<VkDescriptorType>;

    uint32_t                    VertexSize                  = 0;
    vertex_format               VertexFormat                = {};
    std::span<uint32_t const>   VertexShaderCode            = {};
    std::span<uint32_t const>   FragmentShaderCode          = {};
    descriptor_types            DescriptorTypes             = {};
    uint32_t                    PushConstantBufferSize      = 0;
};

static VkResult InternalCreateGraphicsPipeline(
    vulkan_context*     Vulkan,
    vulkan_pipeline*    Pipeline,
    vulkan_graphics_pipeline_configuration const& Config)
{
    VkResult Result = VK_SUCCESS;

    // Create descriptor set layout.
    std::vector<VkDescriptorSetLayoutBinding> DescriptorSetLayoutBindings;
    for (size_t Index = 0; Index < Config.DescriptorTypes.size(); Index++) {
        DescriptorSetLayoutBindings.push_back({
            .binding            = static_cast<uint32_t>(Index),
            .descriptorType     = Config.DescriptorTypes[Index],
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_ALL_GRAPHICS,
            .pImmutableSamplers = nullptr,
        });
    }

    auto DescriptorSetLayoutInfo = VkDescriptorSetLayoutCreateInfo {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(DescriptorSetLayoutBindings.size()),
        .pBindings    = DescriptorSetLayoutBindings.data(),
    };

    Result = vkCreateDescriptorSetLayout(Vulkan->Device, &DescriptorSetLayoutInfo, nullptr, &Pipeline->DescriptorSetLayout);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create descriptor set layout");
        return Result;
    }

    // Create vertex shader module.
    auto VertexShaderModuleInfo = VkShaderModuleCreateInfo {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = Config.VertexShaderCode.size() * sizeof(uint32_t),
        .pCode    = Config.VertexShaderCode.data(),
    };

    VkShaderModule VertexShaderModule;
    Result = vkCreateShaderModule(Vulkan->Device, &VertexShaderModuleInfo, nullptr, &VertexShaderModule);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create vertex shader module");
        return Result;
    }

    // Create fragment shader module.
    auto FragmentShaderModuleInfo = VkShaderModuleCreateInfo {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = Config.FragmentShaderCode.size() * sizeof(uint32_t),
        .pCode    = Config.FragmentShaderCode.data(),
    };

    VkShaderModule FragmentShaderModule;
    Result = vkCreateShaderModule(Vulkan->Device, &FragmentShaderModuleInfo, nullptr, &FragmentShaderModule);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create fragment shader module");
        return Result;
    }

    VkPipelineShaderStageCreateInfo ShaderStageInfos[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = VertexShaderModule,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = FragmentShaderModule,
            .pName  = "main",
        },
    };

    // Dynamic state.
    VkDynamicState DynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    auto DynamicStateInfo = VkPipelineDynamicStateCreateInfo {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(DynamicStates)),
        .pDynamicStates    = DynamicStates,
    };

    // Vertex input state.
    auto VertexBindingDescription = VkVertexInputBindingDescription {
        .binding   = 0,
        .stride    = Config.VertexSize,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    auto VertexInputStateInfo = VkPipelineVertexInputStateCreateInfo {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = Config.VertexSize > 0 ? 1u : 0u,
        .pVertexBindingDescriptions      = &VertexBindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(Config.VertexFormat.size()),
        .pVertexAttributeDescriptions    = Config.VertexFormat.data(),
    };

    // Input assembler state.
    auto InputAssemblyStateInfo = VkPipelineInputAssemblyStateCreateInfo {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // Viewport state.
    auto ViewportStateInfo = VkPipelineViewportStateCreateInfo {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount  = 1,
        .scissorCount   = 1,
    };

    // Rasterizer state.
    auto RasterizationStateInfo = VkPipelineRasterizationStateCreateInfo {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_NONE, //BACK_BIT,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f,
    };

    // Multisample state.
    auto MultisampleStateInfo = VkPipelineMultisampleStateCreateInfo {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples    = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable     = VK_FALSE,
        .minSampleShading        = 1.0f,
        .pSampleMask             = nullptr,
        .alphaToCoverageEnable   = VK_FALSE,
        .alphaToOneEnable        = VK_FALSE,
    };

    // Depth-stencil state.
    auto DepthStencilStateInfo = VkPipelineDepthStencilStateCreateInfo {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable        = VK_FALSE,
        .depthWriteEnable       = VK_FALSE,
        .depthCompareOp         = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable  = VK_FALSE,
        .stencilTestEnable      = VK_FALSE,
        .front                  = {},
        .back                   = {},
        .minDepthBounds         = 0.0f,
        .maxDepthBounds         = 1.0f,
    };

    // Color blend state.
    auto ColorBlendAttachmentState = VkPipelineColorBlendAttachmentState {
        .blendEnable            = VK_TRUE,
        .srcColorBlendFactor    = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp           = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor    = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor    = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp           = VK_BLEND_OP_ADD,
        .colorWriteMask         = VK_COLOR_COMPONENT_R_BIT
                                | VK_COLOR_COMPONENT_G_BIT
                                | VK_COLOR_COMPONENT_B_BIT
                                | VK_COLOR_COMPONENT_A_BIT,
    };
    auto ColorBlendStateInfo = VkPipelineColorBlendStateCreateInfo {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable          = VK_FALSE,
        .logicOp                = VK_LOGIC_OP_COPY,
        .attachmentCount        = 1,
        .pAttachments           = &ColorBlendAttachmentState,
        .blendConstants         = { 0, 0, 0, 0 },
    };

    // Pipeline layout.
    auto PushConstantRange = VkPushConstantRange {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = Config.PushConstantBufferSize,
    };
    auto PipelineLayoutCreateInfo = VkPipelineLayoutCreateInfo {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &Pipeline->DescriptorSetLayout,
        .pushConstantRangeCount = Config.PushConstantBufferSize > 0 ? 1u : 0u,
        .pPushConstantRanges    = &PushConstantRange,
    };

    Result = vkCreatePipelineLayout(Vulkan->Device, &PipelineLayoutCreateInfo, nullptr, &Pipeline->PipelineLayout);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create pipeline layout");
        return Result;
    }

    // Create pipeline.
    auto GraphicsPipelineInfo = VkGraphicsPipelineCreateInfo {
        .sType                  = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount             = static_cast<uint32_t>(std::size(ShaderStageInfos)),
        .pStages                = ShaderStageInfos,
        .pVertexInputState      = &VertexInputStateInfo,
        .pInputAssemblyState    = &InputAssemblyStateInfo,
        .pViewportState         = &ViewportStateInfo,
        .pRasterizationState    = &RasterizationStateInfo,
        .pMultisampleState      = &MultisampleStateInfo,
        .pDepthStencilState     = &DepthStencilStateInfo,
        .pColorBlendState       = &ColorBlendStateInfo,
        .pDynamicState          = &DynamicStateInfo,
        .layout                 = Pipeline->PipelineLayout,
        .renderPass             = Vulkan->MainRenderPass,
        .subpass                = 0,
        .basePipelineHandle     = VK_NULL_HANDLE,
        .basePipelineIndex      = -1,
    };

    Result = vkCreateGraphicsPipelines(Vulkan->Device, VK_NULL_HANDLE, 1, &GraphicsPipelineInfo, nullptr, &Pipeline->Pipeline);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create pipeline");
        return Result;
    }

    vkDestroyShaderModule(Vulkan->Device, VertexShaderModule, nullptr);
    vkDestroyShaderModule(Vulkan->Device, FragmentShaderModule, nullptr);

    return Result;
}

struct vulkan_compute_pipeline_configuration
{
    using descriptor_types = std::vector<VkDescriptorType>;

    std::span<uint32_t const>   ComputeShaderCode           = {};
    descriptor_types            DescriptorTypes             = {};
};

static VkResult InternalCreateComputePipeline(
    vulkan_context*     Vulkan,
    vulkan_pipeline*    Pipeline,
    vulkan_compute_pipeline_configuration const& Config)
{
    VkResult Result = VK_SUCCESS;

    // Create descriptor set layout.
    std::vector<VkDescriptorSetLayoutBinding> DescriptorSetLayoutBindings;
    for (size_t Index = 0; Index < Config.DescriptorTypes.size(); Index++) {
        DescriptorSetLayoutBindings.push_back({
            .binding            = static_cast<uint32_t>(Index),
            .descriptorType     = Config.DescriptorTypes[Index],
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
    }

    auto DescriptorSetLayoutInfo = VkDescriptorSetLayoutCreateInfo {
        .sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount   = static_cast<uint32_t>(DescriptorSetLayoutBindings.size()),
        .pBindings      = DescriptorSetLayoutBindings.data(),
    };

    Result = vkCreateDescriptorSetLayout(Vulkan->Device, &DescriptorSetLayoutInfo, nullptr, &Pipeline->DescriptorSetLayout);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create descriptor set layout");
        return Result;
    }

    // Create compute shader module.
    auto ComputeShaderModuleInfo = VkShaderModuleCreateInfo {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = Config.ComputeShaderCode.size() * sizeof(uint32_t),
        .pCode    = Config.ComputeShaderCode.data(),
    };

    VkShaderModule ComputeShaderModule;

    Result = vkCreateShaderModule(Vulkan->Device, &ComputeShaderModuleInfo, nullptr, &ComputeShaderModule);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create compute shader module");
        return Result;
    }

    auto ComputeShaderStageInfo = VkPipelineShaderStageCreateInfo {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = ComputeShaderModule,
        .pName  = "main",
    };

    // Create pipeline layout.
    auto ComputePipelineLayoutInfo = VkPipelineLayoutCreateInfo {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &Pipeline->DescriptorSetLayout,
    };

    Result = vkCreatePipelineLayout(Vulkan->Device, &ComputePipelineLayoutInfo, nullptr, &Pipeline->PipelineLayout);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create compute pipeline layout");
        return Result;
    }

    // Create pipeline.
    auto ComputePipelineInfo = VkComputePipelineCreateInfo {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage  = ComputeShaderStageInfo,
        .layout = Pipeline->PipelineLayout,
    };

    Result = vkCreateComputePipelines(Vulkan->Device, VK_NULL_HANDLE, 1, &ComputePipelineInfo, nullptr, &Pipeline->Pipeline);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to create compute pipeline");
        return Result;
    }

    vkDestroyShaderModule(Vulkan->Device, ComputeShaderModule, nullptr);

    return Result;
}

static void InternalDestroyPipeline(
    vulkan_context*     Vulkan,
    vulkan_pipeline*    Pipeline)
{
    if (Pipeline->Pipeline)
        vkDestroyPipeline(Vulkan->Device, Pipeline->Pipeline, nullptr);
    if (Pipeline->PipelineLayout)
        vkDestroyPipelineLayout(Vulkan->Device, Pipeline->PipelineLayout, nullptr);
    if (Pipeline->DescriptorSetLayout)
        vkDestroyDescriptorSetLayout(Vulkan->Device, Pipeline->DescriptorSetLayout, nullptr);
}

static VkResult InternalCreateVulkan(
    vulkan_context*     Vulkan,
    GLFWwindow*         Window,
    char const*         ApplicationName)
{
    VkResult Result = VK_SUCCESS;

    std::vector<char const*> RequiredExtensionNames = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    std::vector<char const*> RequiredLayerNames = { "VK_LAYER_KHRONOS_validation" };
    std::vector<char const*> RequiredDeviceExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    // Gather Vulkan extensions required by GLFW.
    {
        uint32_t GlfwExtensionCount = 0;
        const char** GlfwExtensions;
        GlfwExtensions = glfwGetRequiredInstanceExtensions(&GlfwExtensionCount);
        RequiredExtensionNames.reserve(GlfwExtensionCount);
        for (uint32_t I = 0; I < GlfwExtensionCount; I++)
            RequiredExtensionNames.push_back(GlfwExtensions[I]);
    }

    // Check support for validation layers.
    {
        uint32_t LayerCount;
        vkEnumerateInstanceLayerProperties(&LayerCount, nullptr);
        auto Layers = std::vector<VkLayerProperties>(LayerCount);
        vkEnumerateInstanceLayerProperties(&LayerCount, Layers.data());

        for (char const* LayerName : RequiredLayerNames) {
            bool Found = false;
            for (VkLayerProperties const& Layer : Layers) {
                Found = !strcmp(Layer.layerName, LayerName);
                if (Found) break;
            }
            if (!Found) {
                Errorf(Vulkan, "layer '%s' not found", LayerName);
                return VK_ERROR_LAYER_NOT_PRESENT;
            }
        }
    }

    // Create Vulkan instance.
    {
        auto DebugMessengerInfo = VkDebugUtilsMessengerCreateInfoEXT {
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
            .pUserData = Vulkan,
        };

        auto ApplicationInfo = VkApplicationInfo {
            .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName   = ApplicationName,
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName        = nullptr,
            .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion         = VK_API_VERSION_1_0,
        };

        auto InstanceInfo = VkInstanceCreateInfo {
            .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext                   = &DebugMessengerInfo,
            .pApplicationInfo        = &ApplicationInfo,
            .enabledLayerCount       = (uint32_t)RequiredLayerNames.size(),
            .ppEnabledLayerNames     = RequiredLayerNames.data(),
            .enabledExtensionCount   = (uint32_t)RequiredExtensionNames.size(),
            .ppEnabledExtensionNames = RequiredExtensionNames.data(),
        };

        Result = vkCreateInstance(&InstanceInfo, nullptr, &Vulkan->Instance);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create instance");
            return Result;
        }

        Result = CreateDebugUtilsMessengerEXT(Vulkan->Instance, &DebugMessengerInfo, nullptr, &Vulkan->Messenger);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create debug messenger");
            return Result;
        }
    }

    // Create window surface.
    {
        Result = glfwCreateWindowSurface(Vulkan->Instance, Window, nullptr, &Vulkan->Surface);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create window surface");
            return Result;
        }
        Vulkan->Window = Window;
    }

    // Enumerate physical devices and find the most suitable one.
    {
        uint32_t PhysicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(Vulkan->Instance, &PhysicalDeviceCount, nullptr);
        auto PhysicalDevices = std::vector<VkPhysicalDevice>(PhysicalDeviceCount);
        vkEnumeratePhysicalDevices(Vulkan->Instance, &PhysicalDeviceCount, PhysicalDevices.data());

        for (VkPhysicalDevice PhysicalDevice : PhysicalDevices) {
            // Find the required queue families.
            uint32_t QueueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, nullptr);
            auto QueueFamilies = std::vector<VkQueueFamilyProperties>(QueueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, QueueFamilies.data());

            std::optional<uint32_t> GraphicsQueueFamilyIndex;
            std::optional<uint32_t> ComputeQueueFamilyIndex;
            std::optional<uint32_t> PresentQueueFamilyIndex;

            for (uint32_t Index = 0; Index < QueueFamilyCount; Index++) {
                auto const& QueueFamily = QueueFamilies[Index];

                if (!GraphicsQueueFamilyIndex.has_value()) {
                    if (QueueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        GraphicsQueueFamilyIndex = Index;
                }

                if (!ComputeQueueFamilyIndex.has_value()) {
                    if (QueueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                        ComputeQueueFamilyIndex = Index;
                }

                if (!PresentQueueFamilyIndex.has_value()) {
                    VkBool32 PresentSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, Index, Vulkan->Surface, &PresentSupport);
                    if (PresentSupport) PresentQueueFamilyIndex = Index;
                }
            }

            if (!GraphicsQueueFamilyIndex.has_value())
                continue;
            if (!ComputeQueueFamilyIndex.has_value())
                continue;
            if (!PresentQueueFamilyIndex.has_value())
                continue;

            // Ensure the requested device extensions are supported.
            uint32_t DeviceExtensionCount;
            vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &DeviceExtensionCount, nullptr);
            auto DeviceExtensions = std::vector<VkExtensionProperties>(DeviceExtensionCount);
            vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &DeviceExtensionCount, DeviceExtensions.data());

            bool DeviceExtensionsFound = true;
            for (char const* ExtensionName : RequiredDeviceExtensionNames) {
                bool Found = false;
                for (VkExtensionProperties const& Extension : DeviceExtensions) {
                    Found = !strcmp(Extension.extensionName, ExtensionName);
                    if (Found) break;
                }
                if (!Found) {
                    DeviceExtensionsFound = false;
                    break;
                }
            }
            if (!DeviceExtensionsFound)
                continue;

            // Find suitable surface format for the swap chain.
            uint32_t SurfaceFormatCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Vulkan->Surface, &SurfaceFormatCount, nullptr);
            auto SurfaceFormats = std::vector<VkSurfaceFormatKHR>(SurfaceFormatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Vulkan->Surface, &SurfaceFormatCount, SurfaceFormats.data());

            VkSurfaceFormatKHR SurfaceFormat = {};
            bool SurfaceFormatFound = false;
            for (VkSurfaceFormatKHR const& F : SurfaceFormats) {
                if (F.format == VK_FORMAT_B8G8R8A8_SRGB &&
                    F.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    SurfaceFormat = F;
                    SurfaceFormatFound = true;
                }
            }
            if (!SurfaceFormatFound)
                continue;

            // Choose a suitable present mode.
            uint32_t PresentModeCount = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Vulkan->Surface, &PresentModeCount, nullptr);
            auto PresentModes = std::vector<VkPresentModeKHR>(PresentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Vulkan->Surface, &PresentModeCount, PresentModes.data());

            VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
            for (auto const& PM : PresentModes) {
                if (PM == VK_PRESENT_MODE_MAILBOX_KHR)
                    PresentMode = PM;
            }

            // Check physical device features and properties.
            VkPhysicalDeviceFeatures PhysicalDeviceFeatures;
            vkGetPhysicalDeviceFeatures(PhysicalDevice, &PhysicalDeviceFeatures);
            VkPhysicalDeviceProperties PhysicalDeviceProperties;
            vkGetPhysicalDeviceProperties(PhysicalDevice, &PhysicalDeviceProperties);

            // Suitable physical device found.
            Vulkan->PhysicalDevice = PhysicalDevice;
            Vulkan->PhysicalDeviceFeatures = PhysicalDeviceFeatures;
            Vulkan->PhysicalDeviceProperties = PhysicalDeviceProperties;
            Vulkan->GraphicsQueueFamilyIndex = GraphicsQueueFamilyIndex.value();
            Vulkan->ComputeQueueFamilyIndex = ComputeQueueFamilyIndex.value();
            Vulkan->PresentQueueFamilyIndex = PresentQueueFamilyIndex.value();
            Vulkan->SurfaceFormat = SurfaceFormat;
            Vulkan->PresentMode = PresentMode;
            break;
        }

        if (Vulkan->PhysicalDevice == VK_NULL_HANDLE) {
            Errorf(Vulkan, "no suitable physical device");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    // Create logical device.
    {
        const float QueuePriority = 1.0f;

        auto DeviceFeatures = VkPhysicalDeviceFeatures {
            .samplerAnisotropy = VK_TRUE,
        };

        auto QueueFamilyIndices = std::set<uint32_t> {
            Vulkan->GraphicsQueueFamilyIndex,
            Vulkan->ComputeQueueFamilyIndex,
            Vulkan->PresentQueueFamilyIndex,
        };

        auto QueueInfos = std::vector<VkDeviceQueueCreateInfo> {};
        for (uint32_t QueueFamilyIndex : QueueFamilyIndices) {
            QueueInfos.push_back({
                .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = QueueFamilyIndex,
                .queueCount       = 1,
                .pQueuePriorities = &QueuePriority,
            });
        }

        auto DeviceCreateInfo = VkDeviceCreateInfo {
            .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount    = (uint32_t)QueueInfos.size(),
            .pQueueCreateInfos       = QueueInfos.data(),
            .enabledLayerCount       = (uint32_t)RequiredLayerNames.size(),
            .ppEnabledLayerNames     = RequiredLayerNames.data(),
            .enabledExtensionCount   = (uint32_t)RequiredDeviceExtensionNames.size(),
            .ppEnabledExtensionNames = RequiredDeviceExtensionNames.data(),
            .pEnabledFeatures        = &DeviceFeatures,
        };

        Result = vkCreateDevice(Vulkan->PhysicalDevice, &DeviceCreateInfo, nullptr, &Vulkan->Device);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create device");
            return Result;
        }

        vkGetDeviceQueue(Vulkan->Device, Vulkan->GraphicsQueueFamilyIndex, 0, &Vulkan->GraphicsQueue);
        vkGetDeviceQueue(Vulkan->Device, Vulkan->ComputeQueueFamilyIndex, 0, &Vulkan->ComputeQueue);
        vkGetDeviceQueue(Vulkan->Device, Vulkan->PresentQueueFamilyIndex, 0, &Vulkan->PresentQueue);
    }

    // Create graphics and compute command pools.
    {
        auto GraphicsCommandPoolInfo = VkCommandPoolCreateInfo {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = Vulkan->GraphicsQueueFamilyIndex,
        };

        Result = vkCreateCommandPool(Vulkan->Device, &GraphicsCommandPoolInfo, nullptr, &Vulkan->GraphicsCommandPool);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create graphics command pool");
            return Result;
        }

        auto ComputeCommandPoolInfo = VkCommandPoolCreateInfo {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = Vulkan->ComputeQueueFamilyIndex,
        };

        Result = vkCreateCommandPool(Vulkan->Device, &ComputeCommandPoolInfo, nullptr, &Vulkan->ComputeCommandPool);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create compute command pool");
            return Result;
        }
    }

    // Create descriptor pool.
    {
        VkDescriptorPoolSize DescriptorPoolSizes[] = {
            {
                .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 16,
            },
            {
                .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 16,
            },
            {
                .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 16,
            },
            {
                .type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 16,
            },
        };

        auto DescriptorPoolInfo = VkDescriptorPoolCreateInfo {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets       = 16,
            .poolSizeCount = static_cast<uint32_t>(std::size(DescriptorPoolSizes)),
            .pPoolSizes    = DescriptorPoolSizes,
        };

        Result = vkCreateDescriptorPool(Vulkan->Device, &DescriptorPoolInfo, nullptr, &Vulkan->DescriptorPool);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create descriptor pool");
            return Result;
        }
    }

    // Create main render pass.
    {
        auto ColorAttachment = VkAttachmentDescription {
            .format         = Vulkan->SurfaceFormat.format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

        auto ColorAttachmentRef = VkAttachmentReference {
            .attachment     = 0,
            .layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        auto SubpassDesc = VkSubpassDescription {
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &ColorAttachmentRef,
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = nullptr,
        };

        auto Dependency = VkSubpassDependency {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        auto RenderPassInfo = VkRenderPassCreateInfo {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &ColorAttachment,
            .subpassCount    = 1,
            .pSubpasses      = &SubpassDesc,
            .dependencyCount = 1,
            .pDependencies   = &Dependency,
        };

        Result = vkCreateRenderPass(Vulkan->Device, &RenderPassInfo, nullptr, &Vulkan->MainRenderPass);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create main render pass");
            return Result;
        }
    }

    {
        auto SamplerInfo = VkSamplerCreateInfo {
            .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter               = VK_FILTER_LINEAR,
            .minFilter               = VK_FILTER_LINEAR,
            .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias              = 0.0f,
            .anisotropyEnable        = VK_TRUE,
            .maxAnisotropy           = Vulkan->PhysicalDeviceProperties.limits.maxSamplerAnisotropy,
            .compareEnable           = VK_FALSE,
            .compareOp               = VK_COMPARE_OP_ALWAYS,
            .minLod                  = 0.0f,
            .maxLod                  = VK_LOD_CLAMP_NONE,
            .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        Result = vkCreateSampler(Vulkan->Device, &SamplerInfo, nullptr, &Vulkan->ImageSamplerLinear);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create texture sampler");
            return Result;
        }
    }

    {
        auto SamplerInfo = VkSamplerCreateInfo {
            .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter               = VK_FILTER_NEAREST,
            .minFilter               = VK_FILTER_NEAREST,
            .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias              = 0.0f,
            .anisotropyEnable        = VK_FALSE,
            .maxAnisotropy           = 0.0f,
            .compareEnable           = VK_FALSE,
            .compareOp               = VK_COMPARE_OP_ALWAYS,
            .minLod                  = 0.0f,
            .maxLod                  = 0.0f,
            .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        Result = vkCreateSampler(Vulkan->Device, &SamplerInfo, nullptr, &Vulkan->ImageSamplerNearestNoMip);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create sampler");
            return Result;
        }
    }

    {
        auto SamplerInfo = VkSamplerCreateInfo {
            .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter               = VK_FILTER_LINEAR,
            .minFilter               = VK_FILTER_LINEAR,
            .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias              = 0.0f,
            .anisotropyEnable        = VK_FALSE,
            .maxAnisotropy           = 0.0f,
            .compareEnable           = VK_FALSE,
            .compareOp               = VK_COMPARE_OP_ALWAYS,
            .minLod                  = 0.0f,
            .maxLod                  = 0.0f,
            .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        Result = vkCreateSampler(Vulkan->Device, &SamplerInfo, nullptr, &Vulkan->ImageSamplerLinearNoMip);
        if (Result != VK_SUCCESS) {
            Errorf(Vulkan, "failed to create bilinear sampler");
            return Result;
        }
    }

    // Create ImGui resources.
    {
        ImGuiIO& IO = ImGui::GetIO();

        IO.Fonts->AddFontFromMemoryCompressedTTF(CousineRegular_compressed_data, CousineRegular_compressed_size, 16);
        IO.Fonts->Build();

        unsigned char* Data;
        int Width, Height;
        IO.Fonts->GetTexDataAsRGBA32(&Data, &Width, &Height);
        size_t Size = Width * Height * sizeof(uint32_t);

        InternalCreateImage(Vulkan,
            &Vulkan->ImguiTexture,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R8G8B8A8_SRGB,
            { .width = (uint32_t)Width, .height = (uint32_t)Height, .depth = 1},
            0,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            false);

        InternalWriteToDeviceLocalImage(Vulkan,
            &Vulkan->ImguiTexture,
            0, 1,
            Data, (uint32_t)Width, (uint32_t)Height, sizeof(uint32_t),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto ImguiConfig = vulkan_graphics_pipeline_configuration {
            .VertexSize = sizeof(ImDrawVert),
            .VertexFormat = {
                {
                    .location = 0,
                    .binding  = 0,
                    .format   = VK_FORMAT_R32G32_SFLOAT,
                    .offset   = offsetof(ImDrawVert, pos),
                },
                {
                    .location = 1,
                    .binding  = 0,
                    .format   = VK_FORMAT_R32G32_SFLOAT,
                    .offset   = offsetof(ImDrawVert, uv),
                },
                {
                    .location = 2,
                    .binding  = 0,
                    .format   = VK_FORMAT_R8G8B8A8_UNORM,
                    .offset   = offsetof(ImDrawVert, col),
                },
            },
            .VertexShaderCode   = IMGUI_VERTEX_SHADER,
            .FragmentShaderCode = IMGUI_FRAGMENT_SHADER,
            .DescriptorTypes = {
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // TextureArrayNearest
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // TextureBuffer
            },
            .PushConstantBufferSize = sizeof(imgui_push_constant_buffer),
        };

        Result = InternalCreateGraphicsPipeline(Vulkan, &Vulkan->ImguiPipeline, ImguiConfig);
        if (Result != VK_SUCCESS) {
            return Result;
        }
    }

    auto RenderConfig = vulkan_compute_pipeline_configuration {
        .ComputeShaderCode = RENDER_COMPUTE_SHADER,
        .DescriptorTypes = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // FrameUniformBuffer
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   // InputImage
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   // OutputImage
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TextureArrayNearest
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TextureArrayLinear
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // TextureBuffer
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // MaterialBuffer
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // ShapeBuffer
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // ShapeNodeBuffer
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // MeshFaceBuffer
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // MeshFaceExtraBuffer
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // MeshNodeBuffer
        },
    };

    Result = InternalCreateComputePipeline(Vulkan, &Vulkan->RenderPipeline, RenderConfig);
    if (Result != VK_SUCCESS) {
        return Result;
    }

    auto ResolveConfig = vulkan_graphics_pipeline_configuration {
        .VertexSize         = 0,
        .VertexFormat       = {},
        .VertexShaderCode   = RESOLVE_VERTEX_SHADER,
        .FragmentShaderCode = RESOLVE_FRAGMENT_SHADER,
        .DescriptorTypes = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          // FrameUniformBuffer
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        },
    };

    Result = InternalCreateGraphicsPipeline(Vulkan, &Vulkan->ResolvePipeline, ResolveConfig);
    if (Result != VK_SUCCESS) {
        return Result;
    }

    Result = InternalCreatePresentationResources(Vulkan);
    if (Result != VK_SUCCESS) return Result;

    Result = InternalCreateFrameResources(Vulkan);
    if (Result != VK_SUCCESS) return Result;

    return VK_SUCCESS;
}

vulkan_context* CreateVulkan(
    GLFWwindow* Window,
    char const* ApplicationName)
{
    auto Vulkan = new vulkan_context;

    if (InternalCreateVulkan(Vulkan, Window, ApplicationName) != VK_SUCCESS) {
        DestroyVulkan(Vulkan);
        delete Vulkan;
        Vulkan = nullptr;
    }

    return Vulkan;
}

void DestroyVulkan(vulkan_context* Vulkan)
{
    if (Vulkan->Device) {
        // Device exists, make sure there is nothing going on
        // before we start releasing resources.
        vkDeviceWaitIdle(Vulkan->Device);
    }

    InternalDestroyImage(Vulkan, &Vulkan->ImguiTexture);

    InternalDestroyBuffer(Vulkan, &Vulkan->TextureBuffer);
    InternalDestroyBuffer(Vulkan, &Vulkan->MaterialBuffer);
    InternalDestroyBuffer(Vulkan, &Vulkan->ShapeNodeBuffer);
    InternalDestroyBuffer(Vulkan, &Vulkan->ShapeBuffer);
    InternalDestroyBuffer(Vulkan, &Vulkan->MeshNodeBuffer);
    InternalDestroyBuffer(Vulkan, &Vulkan->MeshFaceExtraBuffer);
    InternalDestroyBuffer(Vulkan, &Vulkan->MeshFaceBuffer);
    InternalDestroyImage(Vulkan, &Vulkan->ImageArray);

    InternalDestroyFrameResources(Vulkan);

    // Destroy swap chain and any other window-related resources.
    InternalDestroyPresentationResources(Vulkan);

    InternalDestroyPipeline(Vulkan, &Vulkan->ImguiPipeline);
    InternalDestroyPipeline(Vulkan, &Vulkan->RenderPipeline);
    InternalDestroyPipeline(Vulkan, &Vulkan->ResolvePipeline);

    if (Vulkan->ImageSamplerLinearNoMip) {
        vkDestroySampler(Vulkan->Device, Vulkan->ImageSamplerLinearNoMip, nullptr);
        Vulkan->ImageSamplerLinearNoMip = VK_NULL_HANDLE;
    }

    if (Vulkan->ImageSamplerNearestNoMip) {
        vkDestroySampler(Vulkan->Device, Vulkan->ImageSamplerNearestNoMip, nullptr);
        Vulkan->ImageSamplerNearestNoMip = VK_NULL_HANDLE;
    }

    if (Vulkan->ImageSamplerLinear) {
        vkDestroySampler(Vulkan->Device, Vulkan->ImageSamplerLinear, nullptr);
        Vulkan->ImageSamplerLinear = VK_NULL_HANDLE;
    }

    if (Vulkan->MainRenderPass) {
        vkDestroyRenderPass(Vulkan->Device, Vulkan->MainRenderPass, nullptr);
        Vulkan->MainRenderPass = VK_NULL_HANDLE;
    }

    if (Vulkan->DescriptorPool) {
        vkDestroyDescriptorPool(Vulkan->Device, Vulkan->DescriptorPool, nullptr);
        Vulkan->DescriptorPool = VK_NULL_HANDLE;
    }

    if (Vulkan->GraphicsCommandPool) {
        vkDestroyCommandPool(Vulkan->Device, Vulkan->GraphicsCommandPool, nullptr);
        Vulkan->GraphicsCommandPool = VK_NULL_HANDLE;
    }

    if (Vulkan->ComputeCommandPool) {
        vkDestroyCommandPool(Vulkan->Device, Vulkan->ComputeCommandPool, nullptr);
        Vulkan->ComputeCommandPool = VK_NULL_HANDLE;
    }

    if (Vulkan->Device) {
        vkDestroyDevice(Vulkan->Device, nullptr);
        Vulkan->Device = nullptr;
        Vulkan->GraphicsQueue = VK_NULL_HANDLE;
        Vulkan->ComputeQueue = VK_NULL_HANDLE;
        Vulkan->PresentQueue = VK_NULL_HANDLE;
    }

    if (Vulkan->PhysicalDevice) {
        Vulkan->PhysicalDevice = VK_NULL_HANDLE;
        Vulkan->PhysicalDeviceFeatures = {};
        Vulkan->PhysicalDeviceProperties = {};
        Vulkan->GraphicsQueueFamilyIndex = 0;
        Vulkan->ComputeQueueFamilyIndex = 0;
        Vulkan->PresentQueueFamilyIndex = 0;
        Vulkan->SurfaceFormat = {};
        Vulkan->PresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    if (Vulkan->Messenger) {
        DestroyDebugUtilsMessengerEXT(Vulkan->Instance, Vulkan->Messenger, nullptr);
        Vulkan->Messenger = VK_NULL_HANDLE;
    }

    if (Vulkan->Surface) {
        vkDestroySurfaceKHR(Vulkan->Instance, Vulkan->Surface, nullptr);
        Vulkan->Surface = VK_NULL_HANDLE;
        Vulkan->Window = nullptr;
    }

    if (Vulkan->Instance) {
        vkDestroyInstance(Vulkan->Instance, nullptr);
        Vulkan->Instance = VK_NULL_HANDLE;
    }
}

static void InternalWaitForWindowSize(
    vulkan_context* Vulkan)
{
    int Width = 0, Height = 0;
    glfwGetFramebufferSize(Vulkan->Window, &Width, &Height);
    while (Width == 0 || Height == 0) {
        glfwGetFramebufferSize(Vulkan->Window, &Width, &Height);
        glfwWaitEvents();
    }
}

static void InternalUpdateSceneDataDescriptors(
    vulkan_context* Vulkan)
{
    if (Vulkan->MeshFaceBuffer.Buffer == VK_NULL_HANDLE)
        return;

    auto TextureArrayNearestInfo = VkDescriptorImageInfo {
        .sampler     = Vulkan->ImageSamplerNearestNoMip,
        .imageView   = Vulkan->ImageArray.View,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    auto TextureArrayLinearInfo = VkDescriptorImageInfo {
        .sampler     = Vulkan->ImageSamplerLinearNoMip,
        .imageView   = Vulkan->ImageArray.View,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    auto TextureBufferInfo = VkDescriptorBufferInfo {
        .buffer     = Vulkan->TextureBuffer.Buffer,
        .offset     = 0,
        .range      = Vulkan->TextureBuffer.Size,
    };

    auto MaterialBufferInfo = VkDescriptorBufferInfo {
        .buffer     = Vulkan->MaterialBuffer.Buffer,
        .offset     = 0,
        .range      = Vulkan->MaterialBuffer.Size,
    };

    auto ShapeBufferInfo = VkDescriptorBufferInfo {
        .buffer     = Vulkan->ShapeBuffer.Buffer,
        .offset     = 0,
        .range      = Vulkan->ShapeBuffer.Size,
    };

    auto ShapeNodeBufferInfo = VkDescriptorBufferInfo {
        .buffer     = Vulkan->ShapeNodeBuffer.Buffer,
        .offset     = 0,
        .range      = Vulkan->ShapeNodeBuffer.Size,
    };

    auto MeshFaceBufferInfo = VkDescriptorBufferInfo {
        .buffer     = Vulkan->MeshFaceBuffer.Buffer,
        .offset     = 0,
        .range      = Vulkan->MeshFaceBuffer.Size,
    };

    auto MeshFaceExtraBufferInfo = VkDescriptorBufferInfo {
        .buffer     = Vulkan->MeshFaceExtraBuffer.Buffer,
        .offset     = 0,
        .range      = Vulkan->MeshFaceExtraBuffer.Size,
    };

    auto MeshNodeBufferInfo = VkDescriptorBufferInfo {
        .buffer     = Vulkan->MeshNodeBuffer.Buffer,
        .offset     = 0,
        .range      = Vulkan->MeshNodeBuffer.Size,
    };

    for (int Index = 0; Index < 2; Index++) {
        vulkan_frame_state* Frame = &Vulkan->FrameStates[Index];

        VkWriteDescriptorSet Writes[] = {
            // Rendering descriptors.
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 3,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &TextureArrayNearestInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 4,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &TextureArrayLinearInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 5,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &TextureBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 6,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &MaterialBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 7,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &ShapeBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 8,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &ShapeNodeBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 9,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &MeshFaceBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 10,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &MeshFaceExtraBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->RenderDescriptorSet,
                .dstBinding      = 11,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &MeshNodeBufferInfo,
            },
            // Imgui descriptors.
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->ImguiDescriptorSet,
                .dstBinding      = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &TextureArrayNearestInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = Frame->ImguiDescriptorSet,
                .dstBinding      = 3,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &TextureBufferInfo,
            },
        };

        vkUpdateDescriptorSets(Vulkan->Device, static_cast<uint32_t>(std::size(Writes)), Writes, 0, nullptr);
    }
}

VkResult UploadScene(
    vulkan_context* Vulkan,
    scene const*    Scene,
    uint32_t        DirtyFlags)
{
    VkResult Result = VK_SUCCESS;

    // Scene geometry data is shared between all frame states, so we must
    // wait for all frames to finish rendering before we touch it.
    vkDeviceWaitIdle(Vulkan->Device);

    // Remove the old resources, but don't destroy them yet.
    // We must update descriptors to point to the new ones first.
    vulkan_image ImageArrayOld = {};
    vulkan_buffer TextureBufferOld = {};
    vulkan_buffer MaterialBufferOld = {};
    vulkan_buffer ShapeBufferOld = {};
    vulkan_buffer ShapeNodeBufferOld = {};
    vulkan_buffer MeshFaceBufferOld = {};
    vulkan_buffer MeshFaceExtraBufferOld = {};
    vulkan_buffer MeshNodeBufferOld = {};

    if (DirtyFlags & SCENE_DIRTY_TEXTURES) {
        ImageArrayOld = Vulkan->ImageArray;
        Vulkan->ImageArray = vulkan_image {};

        uint32_t ImageCount = static_cast<uint32_t>(Scene->Images.size());

        // We will create an image even if there are no textures.  This is so
        // that we will always have something to bind for the shader.
        VkImageLayout Layout;
        uint32_t LayerCount;
        if (ImageCount > 0) {
            Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            LayerCount = ImageCount;
        }
        else {
            Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            LayerCount = 1;
        }

        Result = InternalCreateImage(
            Vulkan,
            &Vulkan->ImageArray,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            { .width = 4096, .height = 4096, .depth = 1 },
            LayerCount,
            VK_IMAGE_TILING_OPTIMAL,
            Layout,
            true);
        for (uint32_t Index = 0; Index < ImageCount; Index++) {
            image const& Image = Scene->Images[Index];
            InternalWriteToDeviceLocalImage(Vulkan,
                &Vulkan->ImageArray,
                Index, 1,
                Image.Pixels,
                Image.Width, Image.Height, sizeof(vec4),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        TextureBufferOld = Vulkan->TextureBuffer;
        Vulkan->TextureBuffer = vulkan_buffer {};

        size_t TextureBufferSize = sizeof(packed_texture) * Scene->TexturePack.size();
        Result = InternalCreateBuffer(
            Vulkan,
            &Vulkan->TextureBuffer,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            std::max(1024ull, TextureBufferSize));
        InternalWriteToDeviceLocalBuffer(Vulkan, &Vulkan->TextureBuffer, Scene->TexturePack.data(), TextureBufferSize);
    }

    if (DirtyFlags & SCENE_DIRTY_MATERIALS) {
        MaterialBufferOld = Vulkan->MaterialBuffer;
        Vulkan->MaterialBuffer = vulkan_buffer {};

        size_t MaterialBufferSize = sizeof(packed_material) * Scene->MaterialPack.size();
        Result = InternalCreateBuffer(
            Vulkan,
            &Vulkan->MaterialBuffer,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            std::max(1024ull, MaterialBufferSize));
        InternalWriteToDeviceLocalBuffer(Vulkan, &Vulkan->MaterialBuffer, Scene->MaterialPack.data(), MaterialBufferSize);
    }

    if (DirtyFlags & SCENE_DIRTY_SHAPES) {
        size_t ShapeBufferSize = sizeof(packed_shape) * Scene->ShapePack.size();
        if (ShapeBufferSize > Vulkan->ShapeBuffer.Size) {
            ShapeBufferOld = Vulkan->ShapeBuffer;
            Vulkan->ShapeBuffer = vulkan_buffer {};

            Result = InternalCreateBuffer(
                Vulkan,
                &Vulkan->ShapeBuffer,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                std::max(1024ull, ShapeBufferSize));

        }
        InternalWriteToDeviceLocalBuffer(Vulkan, &Vulkan->ShapeBuffer, Scene->ShapePack.data(), ShapeBufferSize);

        size_t ShapeNodeBufferSize = sizeof(packed_shape_node) * Scene->ShapeNodePack.size();
        if (ShapeNodeBufferSize > Vulkan->ShapeNodeBuffer.Size) {
            ShapeNodeBufferOld = Vulkan->ShapeNodeBuffer;
            Vulkan->ShapeNodeBuffer = vulkan_buffer {};

            Result = InternalCreateBuffer(
                Vulkan,
                &Vulkan->ShapeNodeBuffer,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                std::max(1024ull, ShapeNodeBufferSize));
        }
        InternalWriteToDeviceLocalBuffer(Vulkan, &Vulkan->ShapeNodeBuffer, Scene->ShapeNodePack.data(), ShapeNodeBufferSize);
    }

    if (DirtyFlags & SCENE_DIRTY_MESHES) {
        MeshFaceBufferOld = Vulkan->MeshFaceBuffer;
        Vulkan->MeshFaceBuffer = vulkan_buffer {};
        MeshFaceExtraBufferOld = Vulkan->MeshFaceExtraBuffer;
        Vulkan->MeshFaceExtraBuffer = vulkan_buffer {};
        MeshNodeBufferOld = Vulkan->MeshNodeBuffer;
        Vulkan->MeshNodeBuffer = vulkan_buffer {};

        size_t MeshFaceBufferSize = sizeof(packed_mesh_face) * Scene->MeshFacePack.size();
        Result = InternalCreateBuffer(
            Vulkan,
            &Vulkan->MeshFaceBuffer,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            std::max(1024ull, MeshFaceBufferSize));
        InternalWriteToDeviceLocalBuffer(Vulkan, &Vulkan->MeshFaceBuffer, Scene->MeshFacePack.data(), MeshFaceBufferSize);

        size_t MeshFaceExtraBufferSize = sizeof(packed_mesh_face_extra) * Scene->MeshFaceExtraPack.size();
        Result = InternalCreateBuffer(
            Vulkan,
            &Vulkan->MeshFaceExtraBuffer,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            std::max(1024ull, MeshFaceExtraBufferSize));
        InternalWriteToDeviceLocalBuffer(Vulkan, &Vulkan->MeshFaceExtraBuffer, Scene->MeshFaceExtraPack.data(), MeshFaceExtraBufferSize);

        size_t MeshNodeBufferSize = sizeof(packed_mesh_node) * Scene->MeshNodePack.size();
        Result = InternalCreateBuffer(
            Vulkan,
            &Vulkan->MeshNodeBuffer,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            std::max(1024ull, MeshNodeBufferSize));
        InternalWriteToDeviceLocalBuffer(Vulkan, &Vulkan->MeshNodeBuffer, Scene->MeshNodePack.data(), MeshNodeBufferSize);
    }

    InternalUpdateSceneDataDescriptors(Vulkan);

    InternalDestroyBuffer(Vulkan, &MeshFaceExtraBufferOld);
    InternalDestroyBuffer(Vulkan, &MeshFaceBufferOld);
    InternalDestroyBuffer(Vulkan, &MeshNodeBufferOld);
    InternalDestroyBuffer(Vulkan, &ShapeBufferOld);
    InternalDestroyBuffer(Vulkan, &ShapeNodeBufferOld);
    InternalDestroyBuffer(Vulkan, &MaterialBufferOld);
    InternalDestroyBuffer(Vulkan, &TextureBufferOld);
    InternalDestroyImage(Vulkan, &ImageArrayOld);

    return Result;
}

VkResult RenderFrame(
    vulkan_context*             Vulkan,
    frame_uniform_buffer const* Uniforms,
    ImDrawData*                 ImguiDrawData)
{
    VkResult Result = VK_SUCCESS;

    vulkan_frame_state* Frame0 = &Vulkan->FrameStates[Vulkan->FrameIndex % 2];
    vulkan_frame_state* Frame = &Vulkan->FrameStates[(Vulkan->FrameIndex + 1) % 2];

    Vulkan->FrameIndex++;

    // Wait for the previous commands using this frame state to finish executing.
    vkWaitForFences(Vulkan->Device, 1, &Frame->AvailableFence, VK_TRUE, UINT64_MAX);

    // Try to acquire a swap chain image for us to render to.
    Result = vkAcquireNextImageKHR(Vulkan->Device, Vulkan->SwapChain, UINT64_MAX, Frame->ImageAvailableSemaphore, VK_NULL_HANDLE, &Frame->ImageIndex);

    if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR) {
        InternalWaitForWindowSize(Vulkan);
        vkDeviceWaitIdle(Vulkan->Device);
        InternalDestroyPresentationResources(Vulkan);
        InternalCreatePresentationResources(Vulkan);
        Result = vkAcquireNextImageKHR(Vulkan->Device, Vulkan->SwapChain, UINT64_MAX, Frame->ImageAvailableSemaphore, VK_NULL_HANDLE, &Frame->ImageIndex);
    }

    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to acquire swap chain image");
        return Result;
    }

    // Reset the fence to indicate that the frame state is no longer available.
    vkResetFences(Vulkan->Device, 1, &Frame->AvailableFence);

    InternalWriteToHostVisibleBuffer(Vulkan, &Frame->FrameUniformBuffer, Uniforms, sizeof(frame_uniform_buffer));

    // --- Compute ------------------------------------------------------------

    // Start compute command buffer.
    vkResetCommandBuffer(Frame->ComputeCommandBuffer, 0);
    auto ComputeBeginInfo = VkCommandBufferBeginInfo {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags            = 0,
        .pInheritanceInfo = nullptr,
    };
    Result = vkBeginCommandBuffer(Frame->ComputeCommandBuffer, &ComputeBeginInfo);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to begin recording compute command buffer");
        return Result;
    }

    vkCmdBindPipeline(
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Vulkan->RenderPipeline.Pipeline);

    vkCmdBindDescriptorSets(
        Frame->ComputeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        Vulkan->RenderPipeline.PipelineLayout,
        0, 1, &Frame->RenderDescriptorSet,
        0, nullptr);

    uint32_t GroupPixelSize = 16 * Uniforms->RenderSampleBlockSize;
    uint32_t GroupCountX = (RENDER_WIDTH + GroupPixelSize - 1) / GroupPixelSize;
    uint32_t GroupCountY = (RENDER_HEIGHT + GroupPixelSize - 1) / GroupPixelSize;
    vkCmdDispatch(Frame->ComputeCommandBuffer, GroupCountX, GroupCountY, 1);

    // Copy the render target image into the shader read copy.
    {
        auto SubresourceRange = VkImageSubresourceRange {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        auto PreTransferBarrier = VkImageMemoryBarrier {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = Frame->RenderTarget.Image,
            .subresourceRange    = SubresourceRange,
        };

        vkCmdPipelineBarrier(Frame->ComputeCommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &PreTransferBarrier);

        auto Region = VkImageCopy {
            .srcSubresource = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
            .srcOffset = {
                0, 0, 0
            },
            .dstSubresource = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
            .dstOffset = {
                0, 0, 0
            },
            .extent = {
                RENDER_WIDTH, RENDER_HEIGHT, 1
            }
        };

        vkCmdCopyImage(Frame->ComputeCommandBuffer,
            Frame->RenderTarget.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            Frame->RenderTargetGraphicsCopy.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &Region);

        auto PostTransferBarrier = VkImageMemoryBarrier {
            .sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask          = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask          = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout              = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout              = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = Frame->RenderTarget.Image,
            .subresourceRange    = SubresourceRange,
        };

        vkCmdPipelineBarrier(Frame->ComputeCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &PostTransferBarrier);
    }

    // End compute command buffer.
    Result = vkEndCommandBuffer(Frame->ComputeCommandBuffer);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to end recording compute command buffer");
        return Result;
    }

    VkPipelineStageFlags ComputeWaitStages[] = {
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    };

    VkSemaphore ComputeSignalSemaphores[] = {
        Frame->ComputeToComputeSemaphore,
        Frame->ComputeToGraphicsSemaphore,
    };

    auto ComputeSubmitInfo = VkSubmitInfo {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = Frame0->Fresh ? 0u : 1u,
        .pWaitSemaphores      = &Frame0->ComputeToComputeSemaphore,
        .pWaitDstStageMask    = ComputeWaitStages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &Frame->ComputeCommandBuffer,
        .signalSemaphoreCount = static_cast<uint32_t>(std::size(ComputeSignalSemaphores)),
        .pSignalSemaphores    = ComputeSignalSemaphores,
    };

    Result = vkQueueSubmit(Vulkan->ComputeQueue, 1, &ComputeSubmitInfo, nullptr);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to submit compute command buffer");
        return Result;
    }

    // --- Upload ImGui draw data ---------------------------------------------

    {
        imgui_uniform_buffer ImguiUniformBuffer;
        float L = ImguiDrawData->DisplayPos.x;
        float R = ImguiDrawData->DisplayPos.x + ImguiDrawData->DisplaySize.x;
        float T = ImguiDrawData->DisplayPos.y;
        float B = ImguiDrawData->DisplayPos.y + ImguiDrawData->DisplaySize.y;
        ImguiUniformBuffer.ProjectionMatrix = {
            { 2.0f / (R - L),    0.0f,              0.0f, 0.0f },
            { 0.0f,              2.0f / (B - T),    0.0f, 0.0f },
            { 0.0f,              0.0f,              0.5f, 0.0f },
            { (R + L) / (L - R), (T + B) / (T - B), 0.5f, 1.0f },
        };
        InternalWriteToHostVisibleBuffer(Vulkan, &Frame->ImguiUniformBuffer, &ImguiUniformBuffer, sizeof(imgui_uniform_buffer));

        void* VertexMemory;
        uint32_t VertexOffset = 0;
        vkMapMemory(Vulkan->Device, Frame->ImguiVertexBuffer.Memory, 0, Frame->ImguiVertexBuffer.Size, 0, &VertexMemory);
        void* IndexMemory;
        uint32_t IndexOffset = 0;
        vkMapMemory(Vulkan->Device, Frame->ImguiIndexBuffer.Memory, 0, Frame->ImguiIndexBuffer.Size, 0, &IndexMemory);

        ImDrawVert* VertexPointer = static_cast<ImDrawVert*>(VertexMemory);
        uint16_t* IndexPointer = static_cast<uint16_t*>(IndexMemory);

        for (int I = 0; I < ImguiDrawData->CmdListsCount; I++) {
            ImDrawList* CmdList = ImguiDrawData->CmdLists[I];

            uint32_t VertexDataSize = CmdList->VtxBuffer.Size * sizeof(ImDrawVert);
            memcpy(VertexPointer, CmdList->VtxBuffer.Data, VertexDataSize);
            VertexPointer += CmdList->VtxBuffer.Size;

            uint32_t IndexDataSize = CmdList->IdxBuffer.Size * sizeof(uint16_t);
            memcpy(IndexPointer, CmdList->IdxBuffer.Data, IndexDataSize);
            IndexPointer += CmdList->IdxBuffer.Size;
        }

        vkUnmapMemory(Vulkan->Device, Frame->ImguiIndexBuffer.Memory);
        vkUnmapMemory(Vulkan->Device, Frame->ImguiVertexBuffer.Memory);
    }

    // --- Graphics -----------------------------------------------------------

    // Start graphics command buffer.
    vkResetCommandBuffer(Frame->GraphicsCommandBuffer, 0);
    auto GraphicsBeginInfo = VkCommandBufferBeginInfo {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags            = 0,
        .pInheritanceInfo = nullptr,
    };
    Result = vkBeginCommandBuffer(Frame->GraphicsCommandBuffer, &GraphicsBeginInfo);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to begin recording graphics command buffer");
        return Result;
    }

    // Transition the render target copy for reading from the fragment shader.
    {
        auto SubresourceRange = VkImageSubresourceRange {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        auto Barrier = VkImageMemoryBarrier {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = Frame->RenderTargetGraphicsCopy.Image,
            .subresourceRange    = SubresourceRange,
        };

        vkCmdPipelineBarrier(Frame->GraphicsCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &Barrier);
    }

    {
        VkClearValue ClearValues[] = {
            { .color = {{ 0.0f, 0.0f, 0.0f, 1.0f }} },
            { .depthStencil = { 1.0f, 0 } }
        };

        auto RenderPassBeginInfo = VkRenderPassBeginInfo {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = Vulkan->MainRenderPass,
            .framebuffer     = Vulkan->SwapChainFrameBuffers[Frame->ImageIndex],
            .renderArea      = { .offset = { 0, 0 }, .extent = Vulkan->SwapChainExtent },
            .clearValueCount = 2,
            .pClearValues    = ClearValues,
        };

        vkCmdBeginRenderPass(Frame->GraphicsCommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    //
    {
        vkCmdBindPipeline(
            Frame->GraphicsCommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            Vulkan->ResolvePipeline.Pipeline);

        vkCmdBindDescriptorSets(
            Frame->GraphicsCommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            Vulkan->ResolvePipeline.PipelineLayout,
            0, 1, &Frame->ResolveDescriptorSet,
            0, nullptr);

        auto Viewport = VkViewport {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(Vulkan->SwapChainExtent.width),
            .height   = static_cast<float>(Vulkan->SwapChainExtent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(Frame->GraphicsCommandBuffer, 0, 1, &Viewport);

        auto Scissor = VkRect2D {
            .offset = { 0, 0 },
            .extent = Vulkan->SwapChainExtent,
        };
        vkCmdSetScissor(Frame->GraphicsCommandBuffer, 0, 1, &Scissor);

        vkCmdDraw(Frame->GraphicsCommandBuffer, 6, 1, 0, 0);
    }

    // Render ImGui.
    {
        vkCmdBindPipeline(
            Frame->GraphicsCommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            Vulkan->ImguiPipeline.Pipeline);

        vkCmdBindDescriptorSets(
            Frame->GraphicsCommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            Vulkan->ImguiPipeline.PipelineLayout,
            0, 1, &Frame->ImguiDescriptorSet,
            0, nullptr);

        VkDeviceSize Offset = 0;
        vkCmdBindVertexBuffers(
            Frame->GraphicsCommandBuffer,
            0, 1, &Frame->ImguiVertexBuffer.Buffer, &Offset);

        vkCmdBindIndexBuffer(
            Frame->GraphicsCommandBuffer,
            Frame->ImguiIndexBuffer.Buffer,
            0, VK_INDEX_TYPE_UINT16);

        auto Viewport = VkViewport {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(Vulkan->SwapChainExtent.width),
            .height   = static_cast<float>(Vulkan->SwapChainExtent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(Frame->GraphicsCommandBuffer, 0, 1, &Viewport);

        uint32_t IndexBase = 0;
        uint32_t VertexBase = 0;
        uint32_t PreviousTextureID = 0xFFFFFFFF;

        for (int I = 0; I < ImguiDrawData->CmdListsCount; I++) {
            ImDrawList* CmdList = ImguiDrawData->CmdLists[I];

            for (int j = 0; j < CmdList->CmdBuffer.Size; j++) {
                ImDrawCmd* Cmd = &CmdList->CmdBuffer[j];

                int32_t X0 = static_cast<int32_t>(Cmd->ClipRect.x - ImguiDrawData->DisplayPos.x);
                int32_t Y0 = static_cast<int32_t>(Cmd->ClipRect.y - ImguiDrawData->DisplayPos.y);
                int32_t X1 = static_cast<int32_t>(Cmd->ClipRect.z - ImguiDrawData->DisplayPos.x);
                int32_t Y1 = static_cast<int32_t>(Cmd->ClipRect.w - ImguiDrawData->DisplayPos.y);

                auto scissor = VkRect2D {
                    .offset = { X0, Y0 },
                    .extent = { static_cast<uint32_t>(X1 - X0), static_cast<uint32_t>(Y1 - Y0) },
                };
                vkCmdSetScissor(Frame->GraphicsCommandBuffer, 0, 1, &scissor);

                uint32_t TextureID = static_cast<uint32_t>(reinterpret_cast<size_t>(Cmd->TextureId));
                if (TextureID != PreviousTextureID) {
                    auto PushConstantBuffer = imgui_push_constant_buffer {
                        .TextureID = TextureID,
                    };
                    vkCmdPushConstants(
                        Frame->GraphicsCommandBuffer,
                        Vulkan->ImguiPipeline.PipelineLayout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(imgui_push_constant_buffer), &PushConstantBuffer);
                    PreviousTextureID = TextureID;
                }

                uint32_t IndexCount = Cmd->ElemCount;
                uint32_t FirstIndex = IndexBase + Cmd->IdxOffset;
                uint32_t VertexOffset = VertexBase + Cmd->VtxOffset;
                vkCmdDrawIndexed(Frame->GraphicsCommandBuffer, IndexCount, 1, FirstIndex, VertexOffset, 0);
            }

            IndexBase += CmdList->IdxBuffer.Size;
            VertexBase += CmdList->VtxBuffer.Size;
        }
    }

    vkCmdEndRenderPass(Frame->GraphicsCommandBuffer);

    {
        auto SubresourceRange = VkImageSubresourceRange {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        auto Barrier = VkImageMemoryBarrier {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = Frame->RenderTargetGraphicsCopy.Image,
            .subresourceRange    = SubresourceRange,
        };

        vkCmdPipelineBarrier(Frame->GraphicsCommandBuffer,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &Barrier);
    }

    Result = vkEndCommandBuffer(Frame->GraphicsCommandBuffer);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to end recording graphics command buffer");
        return Result;
    }

    VkPipelineStageFlags GraphicsWaitStages[] = {
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkSemaphore GraphicsWaitSemaphores[] = {
        Frame->ComputeToGraphicsSemaphore,
        Frame->ImageAvailableSemaphore,
    };

    auto GraphicsSubmitInfo = VkSubmitInfo {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = static_cast<uint32_t>(std::size(GraphicsWaitSemaphores)),
        .pWaitSemaphores      = GraphicsWaitSemaphores,
        .pWaitDstStageMask    = GraphicsWaitStages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &Frame->GraphicsCommandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &Frame->ImageFinishedSemaphore,
    };

    Result = vkQueueSubmit(Vulkan->GraphicsQueue, 1, &GraphicsSubmitInfo, Frame->AvailableFence);
    if (Result != VK_SUCCESS) {
        Errorf(Vulkan, "failed to submit graphics command buffer");
        return Result;
    }

    // --- Presentation -------------------------------------------------------

    auto PresentInfo = VkPresentInfoKHR {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &Frame->ImageFinishedSemaphore,
        .swapchainCount     = 1,
        .pSwapchains        = &Vulkan->SwapChain,
        .pImageIndices      = &Frame->ImageIndex,
        .pResults           = nullptr,
    };

    Result = vkQueuePresentKHR(Vulkan->PresentQueue, &PresentInfo);
    if (Result != VK_SUCCESS) {
        if (Result != VK_ERROR_OUT_OF_DATE_KHR && Result != VK_SUBOPTIMAL_KHR)
            Errorf(Vulkan, "failed to present swap chain image");
        return Result;
    }

    Frame->Fresh = false;

    return VK_SUCCESS;
}
