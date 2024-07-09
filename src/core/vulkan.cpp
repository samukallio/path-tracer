#include "core/common.hpp"
#include "core/vulkan.hpp"

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

static void Errorf(vulkan* Vk, char const* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    vfprintf(stderr, Fmt, Args);
    fprintf(stderr, "\n");
    va_end(Args);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback
(
    VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT MessageType,
    const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
    void* UserDataPtr
)
{
    auto Context = static_cast<vulkan*>(UserDataPtr);
    Errorf(Context, CallbackData->pMessage);
    return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT
(
    VkInstance Instance,
    const VkDebugUtilsMessengerCreateInfoEXT* CreateInfo,
    const VkAllocationCallbacks* Allocator,
    VkDebugUtilsMessengerEXT* DebugMessengerOut
)
{
    auto Fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT");
    if (!Fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return Fn(Instance, CreateInfo, Allocator, DebugMessengerOut);
}

static void DestroyDebugUtilsMessengerEXT
(
    VkInstance Instance,
    VkDebugUtilsMessengerEXT DebugMessenger,
    const VkAllocationCallbacks* Allocator
)
{
    auto Fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT");
    if (!Fn) return;
    Fn(Instance, DebugMessenger, Allocator);
}

VkResult CreateVulkanBuffer
(
    vulkan* Vulkan,
    vulkan_buffer* Buffer,
    VkBufferUsageFlags UsageFlags,
    VkMemoryPropertyFlags MemoryFlags,
    VkDeviceSize Size
)
{
    VkResult Result = VK_SUCCESS;

    Buffer->Size = Size;
    Buffer->IsDeviceLocal = (MemoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;

    // Create the buffer.
    VkBufferCreateInfo BufferInfo =
    {
        .sType          = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size           = Size,
        .usage          = UsageFlags,
        .sharingMode    = VK_SHARING_MODE_EXCLUSIVE,
    };

    Result = vkCreateBuffer(Vulkan->Device, &BufferInfo, nullptr, &Buffer->Buffer);
    if (Result != VK_SUCCESS)
    {
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
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; Index++)
    {
        if (!(MemoryRequirements.memoryTypeBits & (1 << Index)))
            continue;
        VkMemoryType& Type = MemoryProperties.memoryTypes[Index];
        if ((Type.propertyFlags & MemoryFlags) != MemoryFlags)
            continue;
        MemoryTypeIndex = Index;
        break;
    }

    // Allocate memory.
    VkMemoryAllocateInfo MemoryAllocateInfo =
    {
        .sType              = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize     = MemoryRequirements.size,
        .memoryTypeIndex    = MemoryTypeIndex,
    };

    Result = vkAllocateMemory(Vulkan->Device, &MemoryAllocateInfo, nullptr, &Buffer->Memory);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to allocate image memory");
        return Result;
    }

    vkBindBufferMemory(Vulkan->Device, Buffer->Buffer, Buffer->Memory, 0);

    return VK_SUCCESS;
}

void DestroyVulkanBuffer
(
    vulkan*         Vulkan,
    vulkan_buffer*  Buffer
)
{
    if (Buffer->Buffer)
        vkDestroyBuffer(Vulkan->Device, Buffer->Buffer, nullptr);
    if (Buffer->Memory)
        vkFreeMemory(Vulkan->Device, Buffer->Memory, nullptr);
}

void WriteToVulkanBuffer
(
    vulkan*         Vulkan,
    vulkan_buffer*  Buffer,
    void const*     Data,
    size_t          Size
)
{
    if (Size == 0) return;

    if (Buffer->IsDeviceLocal)
    {
        // Create a staging buffer and copy the data into it.
        vulkan_buffer Staging;
        CreateVulkanBuffer
        (
            Vulkan, &Staging,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            Buffer->Size
        );

        void* BufferMemory;
        vkMapMemory(Vulkan->Device, Staging.Memory, 0, Buffer->Size, 0, &BufferMemory);
        memcpy(BufferMemory, Data, Size);
        vkUnmapMemory(Vulkan->Device, Staging.Memory);

        // Now copy the data into the device local buffer.
        VkCommandBufferAllocateInfo AllocateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = Vulkan->ComputeCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer CommandBuffer;
        vkAllocateCommandBuffers(Vulkan->Device, &AllocateInfo, &CommandBuffer);

        VkCommandBufferBeginInfo BeginInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(CommandBuffer, &BeginInfo);

        VkBufferCopy Region =
        {
            .srcOffset  = 0,
            .dstOffset  = 0,
            .size       = Buffer->Size,
        };
        vkCmdCopyBuffer(CommandBuffer, Staging.Buffer, Buffer->Buffer, 1, &Region);

        vkEndCommandBuffer(CommandBuffer);

        VkSubmitInfo SubmitInfo =
        {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &CommandBuffer,
        };
        vkQueueSubmit(Vulkan->ComputeQueue, 1, &SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Vulkan->ComputeQueue);

        vkFreeCommandBuffers(Vulkan->Device, Vulkan->ComputeCommandPool, 1, &CommandBuffer);

        // Delete the staging buffer.
        DestroyVulkanBuffer(Vulkan, &Staging);
    }
    else
    {
        void* BufferMemory;
        vkMapMemory(Vulkan->Device, Buffer->Memory, 0, Buffer->Size, 0, &BufferMemory);
        memcpy(BufferMemory, Data, Size);
        vkUnmapMemory(Vulkan->Device, Buffer->Memory);
    }
}

VkResult CreateVulkanImage
(
    vulkan*                 Vulkan,
    vulkan_image*           Image,
    VkImageUsageFlags       UsageFlags,
    VkMemoryPropertyFlags   MemoryFlags,
    VkImageType             Type,
    VkFormat                Format,
    VkExtent3D              Extent,
    uint32_t                LayerCount,
    VkImageTiling           Tiling,
    VkImageLayout           Layout,
    bool                    Compute
)
{
    VkResult Result = VK_SUCCESS;

    Image->Type = Type;
    Image->Format = Format;
    Image->Extent = Extent;
    Image->Tiling = Tiling;
    Image->LayerCount = std::max(1u, LayerCount);

    // Create the image object.
    VkImageCreateInfo ImageInfo =
    {
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
    if (Result != VK_SUCCESS)
    {
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
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; Index++)
    {
        if (!(MemoryRequirements.memoryTypeBits & (1 << Index)))
            continue;
        VkMemoryType& Type = MemoryProperties.memoryTypes[Index];
        if ((Type.propertyFlags & MemoryFlags) != MemoryFlags)
            continue;
        MemoryTypeIndex = Index;
        break;
    }

    // Allocate memory.
    VkMemoryAllocateInfo MemoryAllocateInfo =
    {
        .sType              = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize     = MemoryRequirements.size,
        .memoryTypeIndex    = MemoryTypeIndex,
    };

    Result = vkAllocateMemory(Vulkan->Device, &MemoryAllocateInfo, nullptr, &Image->Memory);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to allocate image memory");
        return Result;
    }

    vkBindImageMemory(Vulkan->Device, Image->Image, Image->Memory, 0);

    VkImageViewType ViewType;

    switch (Type)
    {
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
    auto ImageViewInfo = VkImageViewCreateInfo
    {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image      = Image->Image,
        .viewType   = ViewType,
        .format     = Format,
        .subresourceRange =
        {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = Image->LayerCount,
        },
    };

    Result = vkCreateImageView(Vulkan->Device, &ImageViewInfo, nullptr, &Image->View);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create image view");
        return Result;
    }

    if (Layout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        VkCommandPool CommandPool = Compute
            ? Vulkan->ComputeCommandPool
            : Vulkan->GraphicsCommandPool;

        VkQueue Queue = Compute
            ? Vulkan->ComputeQueue
            : Vulkan->GraphicsQueue;

        VkCommandBufferAllocateInfo AllocateInfo =
        {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = CommandPool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer CommandBuffer;
        vkAllocateCommandBuffers(Vulkan->Device, &AllocateInfo, &CommandBuffer);

        VkCommandBufferBeginInfo BeginInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(CommandBuffer, &BeginInfo);

        VkImageMemoryBarrier Barrier =
        {
            .sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask          = 0,
            .dstAccessMask          = 0,
            .oldLayout              = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout              = Layout,
            .srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
            .image                  = Image->Image,
            .subresourceRange =
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = Image->LayerCount,
            },
        };

        vkCmdPipelineBarrier
        (
            CommandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &Barrier
        );

        vkEndCommandBuffer(CommandBuffer);

        VkSubmitInfo submitInfo =
        {
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

void DestroyVulkanImage
(
    vulkan*         Vulkan,
    vulkan_image*   Image
)
{
    if (Image->View)
        vkDestroyImageView(Vulkan->Device, Image->View, nullptr);
    if (Image->Image)
        vkDestroyImage(Vulkan->Device, Image->Image, nullptr);
    if (Image->Memory)
        vkFreeMemory(Vulkan->Device, Image->Memory, nullptr);
}

VkResult WriteToVulkanImage
(
    vulkan*         Vulkan,
    vulkan_image*   Image,
    uint32_t        LayerIndex,
    uint32_t        LayerCount,
    void const*     Data,
    uint32_t        Width,
    uint32_t        Height,
    uint32_t        BytesPerPixel,
    VkImageLayout   NewLayout
)
{
    size_t Size = Width * Height * BytesPerPixel;

    // Create a staging buffer and copy the data into it.
    vulkan_buffer Staging;
    CreateVulkanBuffer
    (
        Vulkan, &Staging,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        Size
    );
    WriteToVulkanBuffer(Vulkan, &Staging, Data, Size);

    VkCommandBufferAllocateInfo AllocateInfo =
    {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = Vulkan->ComputeCommandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer CommandBuffer;
    vkAllocateCommandBuffers(Vulkan->Device, &AllocateInfo, &CommandBuffer);

    VkCommandBufferBeginInfo BeginInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(CommandBuffer, &BeginInfo);

    VkBufferImageCopy Region =
    {
        .bufferOffset       = 0,
        .bufferRowLength    = Width,
        .bufferImageHeight  = Height,
        .imageSubresource   =
        {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = LayerIndex,
            .layerCount     = LayerCount,
        },
        .imageOffset        = { 0, 0 },
        .imageExtent        = { Width, Height, 1 },
    };

    vkCmdCopyBufferToImage
    (
        CommandBuffer,
        Staging.Buffer, Image->Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &Region
    );

    VkImageMemoryBarrier Barrier =
    {
        .sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
        .image                  = Image->Image,
        .subresourceRange =
        {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = LayerIndex,
            .layerCount     = LayerCount,
        },
    };

    //int32_t srcWidth = image->extent.width;
    //int32_t srcHeight = image->extent.height;

    //for (uint32_t level = 1; level < image->levelCount; level++)
    //{
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
    vkCmdPipelineBarrier
    (
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &Barrier
    );

    vkEndCommandBuffer(CommandBuffer);

    VkSubmitInfo submitInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &CommandBuffer,
    };
    vkQueueSubmit(Vulkan->ComputeQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(Vulkan->ComputeQueue);

    vkFreeCommandBuffers(Vulkan->Device, Vulkan->ComputeCommandPool, 1, &CommandBuffer);

    // Delete the staging buffer.
    DestroyVulkanBuffer(Vulkan, &Staging);

    return VK_SUCCESS;
}

VkResult CreateVulkanDescriptorSetLayout
(
    vulkan*                     Vulkan,
    VkDescriptorSetLayout*      Layout,
    std::span<VkDescriptorType> DescriptorTypes
)
{
    VkResult Result = VK_SUCCESS;

    std::vector<VkDescriptorSetLayoutBinding> DescriptorSetLayoutBindings;
    for (size_t Index = 0; Index < DescriptorTypes.size(); Index++)
    {
        DescriptorSetLayoutBindings.push_back
        ({
            .binding            = static_cast<uint32_t>(Index),
            .descriptorType     = DescriptorTypes[Index],
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_ALL,
            .pImmutableSamplers = nullptr,
        });
    }

    auto DescriptorSetLayoutInfo = VkDescriptorSetLayoutCreateInfo
    {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(DescriptorSetLayoutBindings.size()),
        .pBindings    = DescriptorSetLayoutBindings.data(),
    };

    Result = vkCreateDescriptorSetLayout(Vulkan->Device, &DescriptorSetLayoutInfo, nullptr, Layout);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create descriptor set layout");
        return Result;
    }

    return Result;
}

void DestroyVulkanDescriptorSetLayout
(
    vulkan* Vulkan,
    VkDescriptorSetLayout* Layout
)
{
    if (*Layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Vulkan->Device, *Layout, nullptr);
        *Layout = VK_NULL_HANDLE;
    }
}

void UpdateVulkanDescriptorSet
(
    vulkan*                      Vulkan,
    VkDescriptorSet              DescriptorSet,
    std::span<vulkan_descriptor> Descriptors
)
{
    assert(Descriptors.size() <= 16);

    if (Descriptors.empty()) return;

    VkWriteDescriptorSet Writes[16] = {};
    VkDescriptorBufferInfo BufferInfo[16];
    VkDescriptorImageInfo ImageInfo[16];

    for (uint Binding = 0; Binding < std::size(Descriptors); Binding++)
    {
        vulkan_descriptor* Descriptor = &Descriptors[Binding];

        Writes[Binding] =
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = DescriptorSet,
            .dstBinding      = Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = Descriptor->Type,
        };

        switch (Descriptor->Type)
        {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                BufferInfo[Binding].buffer = Descriptor->Buffer->Buffer;
                BufferInfo[Binding].offset = 0;
                BufferInfo[Binding].range = Descriptor->Buffer->Size;
                Writes[Binding].pBufferInfo = &BufferInfo[Binding];
                break;

            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                ImageInfo[Binding].sampler = Descriptor->Sampler;
                [[fallthrough]];
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                ImageInfo[Binding].imageView = Descriptor->Image->View;
                ImageInfo[Binding].imageLayout = Descriptor->ImageLayout;
                Writes[Binding].pImageInfo = &ImageInfo[Binding];
                break;
        }
    }

    vkUpdateDescriptorSets(Vulkan->Device, static_cast<uint32_t>(std::size(Descriptors)), Writes, 0, nullptr);
}

VkResult CreateVulkanDescriptorSet
(
    vulkan*                      Vulkan,
    VkDescriptorSetLayout        DescriptorSetLayout,
    VkDescriptorSet*             DescriptorSet,
    std::span<vulkan_descriptor> Descriptors
)
{
    VkResult Result = VK_SUCCESS;

    VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo =
    {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = Vulkan->DescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &DescriptorSetLayout,
    };

    Result = vkAllocateDescriptorSets(Vulkan->Device, &DescriptorSetAllocateInfo, DescriptorSet);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to allocate descriptor set");
        return Result;
    }

    UpdateVulkanDescriptorSet(Vulkan, *DescriptorSet, Descriptors);
    return Result;
}

void DestroyVulkanDescriptorSet
(
    vulkan* Vulkan,
    VkDescriptorSet* Set
)
{
    if (*Set != VK_NULL_HANDLE)
    {
        vkFreeDescriptorSets(Vulkan->Device, Vulkan->DescriptorPool, 1, Set);
        *Set = VK_NULL_HANDLE;
    }
}

static VkResult InternalCreatePresentationResources(vulkan* Vulkan)
{
    VkResult Result = VK_SUCCESS;

    // Create the swap chain.
    {
        // Determine current window surface capabilities.
        VkSurfaceCapabilitiesKHR SurfaceCapabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Vulkan->PhysicalDevice, Vulkan->Surface, &SurfaceCapabilities);

        // Determine width and height of the swap chain.
        VkExtent2D ImageExtent = SurfaceCapabilities.currentExtent;
        if (ImageExtent.width == 0xFFFFFFFF)
        {
            int Width, Height;
            glfwGetFramebufferSize(Vulkan->Window, &Width, &Height);
            ImageExtent.width = std::clamp
            (
                static_cast<uint32_t>(Width),
                SurfaceCapabilities.minImageExtent.width,
                SurfaceCapabilities.maxImageExtent.width
            );
            ImageExtent.height = std::clamp
            (
                static_cast<uint32_t>(Height),
                SurfaceCapabilities.minImageExtent.height,
                SurfaceCapabilities.maxImageExtent.height
            );
        }

        // Determine swap chain image count.
        uint32_t ImageCount = SurfaceCapabilities.minImageCount + 1;
        if (SurfaceCapabilities.maxImageCount > 0)
            ImageCount = std::min(ImageCount, SurfaceCapabilities.maxImageCount);

        auto SwapChainInfo = VkSwapchainCreateInfoKHR
        {
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

        if (Vulkan->GraphicsQueueFamilyIndex == Vulkan->PresentQueueFamilyIndex)
        {
            SwapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            SwapChainInfo.queueFamilyIndexCount = 0;
            SwapChainInfo.pQueueFamilyIndices = nullptr;
        }
        else
        {
            uint32_t const QueueFamilyIndices[] = {
                Vulkan->GraphicsQueueFamilyIndex,
                Vulkan->PresentQueueFamilyIndex
            };
            SwapChainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            SwapChainInfo.queueFamilyIndexCount = 2;
            SwapChainInfo.pQueueFamilyIndices = QueueFamilyIndices;
        }

        Result = vkCreateSwapchainKHR(Vulkan->Device, &SwapChainInfo, nullptr, &Vulkan->SwapChain);
        if (Result != VK_SUCCESS)
        {
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

        for (VkImage Image : Images)
        {
            Vulkan->SwapChainImages.push_back(Image);

            auto ImageViewInfo = VkImageViewCreateInfo
            {
                .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image      = Image,
                .viewType   = VK_IMAGE_VIEW_TYPE_2D,
                .format     = Vulkan->SwapChainFormat,
                .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                .subresourceRange =
                {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                }
            };

            VkImageView ImageView;

            Result = vkCreateImageView(Vulkan->Device, &ImageViewInfo, nullptr, &ImageView);
            if (Result != VK_SUCCESS)
            {
                Errorf(Vulkan, "failed to create image view");
                return Result;
            }

            Vulkan->SwapChainImageViews.push_back(ImageView);

            auto FrameBufferInfo = VkFramebufferCreateInfo
            {
                .sType              = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass         = Vulkan->RenderPass,
                .attachmentCount    = 1,
                .pAttachments       = &ImageView,
                .width              = Vulkan->SwapChainExtent.width,
                .height             = Vulkan->SwapChainExtent.height,
                .layers             = 1,
            };

            VkFramebuffer FrameBuffer;

            Result = vkCreateFramebuffer(Vulkan->Device, &FrameBufferInfo, nullptr, &FrameBuffer);
            if (Result != VK_SUCCESS)
            {
                Errorf(Vulkan, "failed to create framebuffer");
                return Result;
            }

            Vulkan->SwapChainFrameBuffers.push_back(FrameBuffer);
        }
    }

    return VK_SUCCESS;
}

static void InternalDestroyPresentationResources(vulkan* Vulkan)
{
    for (VkFramebuffer FrameBuffer : Vulkan->SwapChainFrameBuffers)
        vkDestroyFramebuffer(Vulkan->Device, FrameBuffer, nullptr);
    Vulkan->SwapChainFrameBuffers.clear();

    for (VkImageView ImageView : Vulkan->SwapChainImageViews)
        vkDestroyImageView(Vulkan->Device, ImageView, nullptr);
    Vulkan->SwapChainImageViews.clear();

    if (Vulkan->SwapChain)
    {
        vkDestroySwapchainKHR(Vulkan->Device, Vulkan->SwapChain, nullptr);
        Vulkan->SwapChain = nullptr;
        Vulkan->SwapChainExtent = {};
        Vulkan->SwapChainFormat = VK_FORMAT_UNDEFINED;
        Vulkan->SwapChainImages.clear();
    }
}

static VkResult InternalCreateFrameResources(vulkan* Vulkan)
{
    VkResult Result = VK_SUCCESS;

    for (int Index = 0; Index < 2; Index++)
    {
        vulkan_frame* Frame = &Vulkan->FrameStates[Index];

        Frame->Index = Index;
        Frame->Fresh = true;

        auto GraphicsCommandBufferAllocateInfo = VkCommandBufferAllocateInfo
        {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = Vulkan->GraphicsCommandPool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        Result = vkAllocateCommandBuffers(Vulkan->Device, &GraphicsCommandBufferAllocateInfo, &Frame->GraphicsCommandBuffer);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to allocate graphics command buffer");
            return Result;
        }

        auto ComputeCommandBufferAllocateInfo = VkCommandBufferAllocateInfo
        {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = Vulkan->ComputeCommandPool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        Result = vkAllocateCommandBuffers(Vulkan->Device, &ComputeCommandBufferAllocateInfo, &Frame->ComputeCommandBuffer);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to allocate compute command buffer");
            return Result;
        }

        auto SemaphoreInfo = VkSemaphoreCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };

        VkSemaphore* SemaphorePtrs[] =
        {
            &Frame->ImageAvailableSemaphore,
            &Frame->ImageFinishedSemaphore,
            &Frame->ComputeToComputeSemaphore,
            &Frame->ComputeToGraphicsSemaphore,
        };

        for (VkSemaphore* SemaphorePtr : SemaphorePtrs)
        {
            Result = vkCreateSemaphore(Vulkan->Device, &SemaphoreInfo, nullptr, SemaphorePtr);
            if (Result != VK_SUCCESS)
            {
                Errorf(Vulkan, "failed to create semaphore");
                return Result;
            }
        }

        auto FenceInfo = VkFenceCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        VkFence* FencePtrs[] =
        {
            &Frame->AvailableFence,
        };

        for (VkFence* FencePtr : FencePtrs)
        {
            Result = vkCreateFence(Vulkan->Device, &FenceInfo, nullptr, FencePtr);
            if (Result != VK_SUCCESS)
            {
                Errorf(Vulkan, "failed to create semaphore");
                return Result;
            }
        }
    }

    Vulkan->FrameStates[0].Previous = &Vulkan->FrameStates[1];
    Vulkan->FrameStates[1].Previous = &Vulkan->FrameStates[0];

    return VK_SUCCESS;
}

static VkResult InternalDestroyFrameResources(vulkan* Vulkan)
{
    for (int Index = 0; Index < 2; Index++)
    {
        vulkan_frame* Frame = &Vulkan->FrameStates[Index];

        vkDestroySemaphore(Vulkan->Device, Frame->ComputeToComputeSemaphore, nullptr);
        vkDestroySemaphore(Vulkan->Device, Frame->ComputeToGraphicsSemaphore, nullptr);
        vkDestroySemaphore(Vulkan->Device, Frame->ImageAvailableSemaphore, nullptr);
        vkDestroySemaphore(Vulkan->Device, Frame->ImageFinishedSemaphore, nullptr);
        vkDestroyFence(Vulkan->Device, Frame->AvailableFence, nullptr);
    }

    return VK_SUCCESS;
}

VkResult CreateVulkanGraphicsPipeline
(
    vulkan*             Vulkan,
    vulkan_pipeline*    Pipeline,
    vulkan_graphics_pipeline_configuration const& Config
)
{
    VkResult Result = VK_SUCCESS;

    // Create vertex shader module.
    auto VertexShaderModuleInfo = VkShaderModuleCreateInfo
    {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = Config.VertexShaderCode.size() * sizeof(uint32_t),
        .pCode    = Config.VertexShaderCode.data(),
    };

    VkShaderModule VertexShaderModule;
    Result = vkCreateShaderModule(Vulkan->Device, &VertexShaderModuleInfo, nullptr, &VertexShaderModule);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create vertex shader module");
        return Result;
    }

    // Create fragment shader module.
    auto FragmentShaderModuleInfo = VkShaderModuleCreateInfo
    {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = Config.FragmentShaderCode.size() * sizeof(uint32_t),
        .pCode    = Config.FragmentShaderCode.data(),
    };

    VkShaderModule FragmentShaderModule;
    Result = vkCreateShaderModule(Vulkan->Device, &FragmentShaderModuleInfo, nullptr, &FragmentShaderModule);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create fragment shader module");
        return Result;
    }

    VkPipelineShaderStageCreateInfo ShaderStageInfos[] =
    {
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
    VkDynamicState DynamicStates[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    auto DynamicStateInfo = VkPipelineDynamicStateCreateInfo
    {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(DynamicStates)),
        .pDynamicStates    = DynamicStates,
    };

    // Vertex input state.
    auto VertexBindingDescription = VkVertexInputBindingDescription
    {
        .binding   = 0,
        .stride    = Config.VertexSize,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    auto VertexInputStateInfo = VkPipelineVertexInputStateCreateInfo
    {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = Config.VertexSize > 0 ? 1u : 0u,
        .pVertexBindingDescriptions      = &VertexBindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(Config.VertexFormat.size()),
        .pVertexAttributeDescriptions    = Config.VertexFormat.data(),
    };

    // Input assembler state.
    auto InputAssemblyStateInfo = VkPipelineInputAssemblyStateCreateInfo
    {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // Viewport state.
    auto ViewportStateInfo = VkPipelineViewportStateCreateInfo
    {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount  = 1,
        .scissorCount   = 1,
    };

    // Rasterizer state.
    auto RasterizationStateInfo = VkPipelineRasterizationStateCreateInfo
    {
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
    auto MultisampleStateInfo = VkPipelineMultisampleStateCreateInfo
    {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples    = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable     = VK_FALSE,
        .minSampleShading        = 1.0f,
        .pSampleMask             = nullptr,
        .alphaToCoverageEnable   = VK_FALSE,
        .alphaToOneEnable        = VK_FALSE,
    };

    // Depth-stencil state.
    auto DepthStencilStateInfo = VkPipelineDepthStencilStateCreateInfo
    {
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
    auto ColorBlendAttachmentState = VkPipelineColorBlendAttachmentState
    {
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
    auto ColorBlendStateInfo = VkPipelineColorBlendStateCreateInfo
    {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable          = VK_FALSE,
        .logicOp                = VK_LOGIC_OP_COPY,
        .attachmentCount        = 1,
        .pAttachments           = &ColorBlendAttachmentState,
        .blendConstants         = { 0, 0, 0, 0 },
    };

    // Pipeline layout.
    auto PushConstantRange = VkPushConstantRange
    {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = Config.PushConstantBufferSize,
    };
    auto PipelineLayoutCreateInfo = VkPipelineLayoutCreateInfo
    {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = static_cast<uint32_t>(Config.DescriptorSetLayouts.size()),
        .pSetLayouts            = Config.DescriptorSetLayouts.data(),
        .pushConstantRangeCount = Config.PushConstantBufferSize > 0 ? 1u : 0u,
        .pPushConstantRanges    = &PushConstantRange,
    };

    Result = vkCreatePipelineLayout(Vulkan->Device, &PipelineLayoutCreateInfo, nullptr, &Pipeline->PipelineLayout);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create pipeline layout");
        return Result;
    }

    // Create pipeline.
    auto GraphicsPipelineInfo = VkGraphicsPipelineCreateInfo
    {
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
        .renderPass             = Vulkan->RenderPass,
        .subpass                = 0,
        .basePipelineHandle     = VK_NULL_HANDLE,
        .basePipelineIndex      = -1,
    };

    Result = vkCreateGraphicsPipelines(Vulkan->Device, VK_NULL_HANDLE, 1, &GraphicsPipelineInfo, nullptr, &Pipeline->Pipeline);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create pipeline");
        return Result;
    }

    vkDestroyShaderModule(Vulkan->Device, VertexShaderModule, nullptr);
    vkDestroyShaderModule(Vulkan->Device, FragmentShaderModule, nullptr);

    return Result;
}

VkResult CreateVulkanComputePipeline
(
    vulkan*             Vulkan,
    vulkan_pipeline*    Pipeline,
    vulkan_compute_pipeline_configuration const& Config
)
{
    VkResult Result = VK_SUCCESS;

    // Create compute shader module.
    auto ComputeShaderModuleInfo = VkShaderModuleCreateInfo
    {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = Config.ComputeShaderCode.size() * sizeof(uint32_t),
        .pCode    = Config.ComputeShaderCode.data(),
    };

    VkShaderModule ComputeShaderModule;

    Result = vkCreateShaderModule(Vulkan->Device, &ComputeShaderModuleInfo, nullptr, &ComputeShaderModule);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create compute shader module");
        return Result;
    }

    auto ComputeShaderStageInfo = VkPipelineShaderStageCreateInfo
    {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = ComputeShaderModule,
        .pName  = "main",
    };

    // Create pipeline layout.
    auto PushConstantRange = VkPushConstantRange
    {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = Config.PushConstantBufferSize,
    };
    auto ComputePipelineLayoutInfo = VkPipelineLayoutCreateInfo
    {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = static_cast<uint32_t>(Config.DescriptorSetLayouts.size()),
        .pSetLayouts            = Config.DescriptorSetLayouts.data(),
        .pushConstantRangeCount = Config.PushConstantBufferSize > 0 ? 1u : 0u,
        .pPushConstantRanges    = &PushConstantRange,
    };

    Result = vkCreatePipelineLayout(Vulkan->Device, &ComputePipelineLayoutInfo, nullptr, &Pipeline->PipelineLayout);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create compute pipeline layout");
        return Result;
    }

    // Create pipeline.
    auto ComputePipelineInfo = VkComputePipelineCreateInfo
    {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage  = ComputeShaderStageInfo,
        .layout = Pipeline->PipelineLayout,
    };

    Result = vkCreateComputePipelines(Vulkan->Device, VK_NULL_HANDLE, 1, &ComputePipelineInfo, nullptr, &Pipeline->Pipeline);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to create compute pipeline");
        return Result;
    }

    vkDestroyShaderModule(Vulkan->Device, ComputeShaderModule, nullptr);

    return Result;
}

void DestroyVulkanPipeline
(
    vulkan*             Vulkan,
    vulkan_pipeline*    Pipeline
)
{
    if (Pipeline->Pipeline)
        vkDestroyPipeline(Vulkan->Device, Pipeline->Pipeline, nullptr);
    if (Pipeline->PipelineLayout)
        vkDestroyPipelineLayout(Vulkan->Device, Pipeline->PipelineLayout, nullptr);
}

static VkResult InternalCreateVulkan
(
    vulkan*             Vulkan,
    GLFWwindow*         Window,
    char const*         ApplicationName
)
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

        for (char const* LayerName : RequiredLayerNames)
        {
            bool Found = false;
            for (VkLayerProperties const& Layer : Layers)
            {
                Found = !strcmp(Layer.layerName, LayerName);
                if (Found) break;
            }
            if (!Found)
            {
                Errorf(Vulkan, "layer '%s' not found", LayerName);
                return VK_ERROR_LAYER_NOT_PRESENT;
            }
        }
    }

    // Create Vulkan instance.
    {
        auto DebugMessengerInfo = VkDebugUtilsMessengerCreateInfoEXT
        {
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

        auto ApplicationInfo = VkApplicationInfo
        {
            .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName   = ApplicationName,
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName        = nullptr,
            .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion         = VK_API_VERSION_1_0,
        };

        auto InstanceInfo = VkInstanceCreateInfo
        {
            .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext                   = &DebugMessengerInfo,
            .pApplicationInfo        = &ApplicationInfo,
            .enabledLayerCount       = (uint32_t)RequiredLayerNames.size(),
            .ppEnabledLayerNames     = RequiredLayerNames.data(),
            .enabledExtensionCount   = (uint32_t)RequiredExtensionNames.size(),
            .ppEnabledExtensionNames = RequiredExtensionNames.data(),
        };

        Result = vkCreateInstance(&InstanceInfo, nullptr, &Vulkan->Instance);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create instance");
            return Result;
        }

        Result = CreateDebugUtilsMessengerEXT(Vulkan->Instance, &DebugMessengerInfo, nullptr, &Vulkan->Messenger);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create debug messenger");
            return Result;
        }
    }

    // Create window surface.
    {
        Result = glfwCreateWindowSurface(Vulkan->Instance, Window, nullptr, &Vulkan->Surface);
        if (Result != VK_SUCCESS)
        {
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

        for (VkPhysicalDevice PhysicalDevice : PhysicalDevices)
        {
            // Find the required queue families.
            uint32_t QueueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, nullptr);
            auto QueueFamilies = std::vector<VkQueueFamilyProperties>(QueueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, QueueFamilies.data());

            std::optional<uint32_t> GraphicsQueueFamilyIndex;
            std::optional<uint32_t> ComputeQueueFamilyIndex;
            std::optional<uint32_t> PresentQueueFamilyIndex;

            for (uint32_t Index = 0; Index < QueueFamilyCount; Index++)
            {
                auto const& QueueFamily = QueueFamilies[Index];

                if (!GraphicsQueueFamilyIndex.has_value())
                {
                    if (QueueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        GraphicsQueueFamilyIndex = Index;
                }

                if (!ComputeQueueFamilyIndex.has_value())
                {
                    if (QueueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                        ComputeQueueFamilyIndex = Index;
                }

                if (!PresentQueueFamilyIndex.has_value())
                {
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
            for (char const* ExtensionName : RequiredDeviceExtensionNames)
            {
                bool Found = false;
                for (VkExtensionProperties const& Extension : DeviceExtensions)
                {
                    Found = !strcmp(Extension.extensionName, ExtensionName);
                    if (Found) break;
                }
                if (!Found)
                {
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
            for (VkSurfaceFormatKHR const& F : SurfaceFormats)
            {
                if (F.format == VK_FORMAT_B8G8R8A8_SRGB && F.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
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
            for (auto const& PM : PresentModes)
            {
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

        if (Vulkan->PhysicalDevice == VK_NULL_HANDLE)
        {
            Errorf(Vulkan, "no suitable physical device");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    // Create logical device.
    {
        const float QueuePriority = 1.0f;

        auto DeviceFeatures = VkPhysicalDeviceFeatures
        {
            .samplerAnisotropy = VK_TRUE,
        };

        auto QueueFamilyIndices = std::set<uint32_t>
        {
            Vulkan->GraphicsQueueFamilyIndex,
            Vulkan->ComputeQueueFamilyIndex,
            Vulkan->PresentQueueFamilyIndex,
        };

        auto QueueInfos = std::vector<VkDeviceQueueCreateInfo> {};
        for (uint32_t QueueFamilyIndex : QueueFamilyIndices)
        {
            QueueInfos.push_back
            ({
                .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = QueueFamilyIndex,
                .queueCount       = 1,
                .pQueuePriorities = &QueuePriority,
            });
        }

        auto DeviceCreateInfo = VkDeviceCreateInfo
        {
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
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create device");
            return Result;
        }

        vkGetDeviceQueue(Vulkan->Device, Vulkan->GraphicsQueueFamilyIndex, 0, &Vulkan->GraphicsQueue);
        vkGetDeviceQueue(Vulkan->Device, Vulkan->ComputeQueueFamilyIndex, 0, &Vulkan->ComputeQueue);
        vkGetDeviceQueue(Vulkan->Device, Vulkan->PresentQueueFamilyIndex, 0, &Vulkan->PresentQueue);
    }

    // Create graphics and compute command pools.
    {
        auto GraphicsCommandPoolInfo = VkCommandPoolCreateInfo
        {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = Vulkan->GraphicsQueueFamilyIndex,
        };

        Result = vkCreateCommandPool(Vulkan->Device, &GraphicsCommandPoolInfo, nullptr, &Vulkan->GraphicsCommandPool);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create graphics command pool");
            return Result;
        }

        auto ComputeCommandPoolInfo = VkCommandPoolCreateInfo
        {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = Vulkan->ComputeQueueFamilyIndex,
        };

        Result = vkCreateCommandPool(Vulkan->Device, &ComputeCommandPoolInfo, nullptr, &Vulkan->ComputeCommandPool);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create compute command pool");
            return Result;
        }
    }

    // Create descriptor pool.
    {
        VkDescriptorPoolSize DescriptorPoolSizes[] =
        {
            {
                .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 32,
            },
            {
                .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 32,
            },
            {
                .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 32,
            },
            {
                .type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 32,
            },
        };

        auto DescriptorPoolInfo = VkDescriptorPoolCreateInfo
        {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets       = 16,
            .poolSizeCount = static_cast<uint32_t>(std::size(DescriptorPoolSizes)),
            .pPoolSizes    = DescriptorPoolSizes,
        };

        Result = vkCreateDescriptorPool(Vulkan->Device, &DescriptorPoolInfo, nullptr, &Vulkan->DescriptorPool);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create descriptor pool");
            return Result;
        }
    }

    // Create render pass.
    {
        auto ColorAttachment = VkAttachmentDescription
        {
            .format         = Vulkan->SurfaceFormat.format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

        auto ColorAttachmentRef = VkAttachmentReference
        {
            .attachment     = 0,
            .layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        auto SubpassDesc = VkSubpassDescription
        {
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &ColorAttachmentRef,
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = nullptr,
        };

        auto Dependency = VkSubpassDependency
        {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        auto RenderPassInfo = VkRenderPassCreateInfo
        {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &ColorAttachment,
            .subpassCount    = 1,
            .pSubpasses      = &SubpassDesc,
            .dependencyCount = 1,
            .pDependencies   = &Dependency,
        };

        Result = vkCreateRenderPass(Vulkan->Device, &RenderPassInfo, nullptr, &Vulkan->RenderPass);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create main render pass");
            return Result;
        }
    }

    {
        auto SamplerInfo = VkSamplerCreateInfo
        {
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
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create texture sampler");
            return Result;
        }
    }

    {
        auto SamplerInfo = VkSamplerCreateInfo
        {
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
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create sampler");
            return Result;
        }
    }

    {
        auto SamplerInfo = VkSamplerCreateInfo
        {
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
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to create bilinear sampler");
            return Result;
        }
    }

    Result = InternalCreatePresentationResources(Vulkan);
    if (Result != VK_SUCCESS) return Result;

    Result = InternalCreateFrameResources(Vulkan);
    if (Result != VK_SUCCESS) return Result;

    return VK_SUCCESS;
}

vulkan* CreateVulkan
(
    GLFWwindow* Window,
    char const* ApplicationName
)
{
    auto Vulkan = new vulkan;

    if (InternalCreateVulkan(Vulkan, Window, ApplicationName) != VK_SUCCESS)
    {
        DestroyVulkan(Vulkan);
        delete Vulkan;
        Vulkan = nullptr;
    }

    return Vulkan;
}

void DestroyVulkan(vulkan* Vulkan)
{
    if (Vulkan->Device)
    {
        // Device exists, make sure there is nothing going on
        // before we start releasing resources.
        vkDeviceWaitIdle(Vulkan->Device);
    }

    InternalDestroyFrameResources(Vulkan);

    // Destroy swap chain and any other window-related resources.
    InternalDestroyPresentationResources(Vulkan);

    if (Vulkan->ImageSamplerLinearNoMip)
    {
        vkDestroySampler(Vulkan->Device, Vulkan->ImageSamplerLinearNoMip, nullptr);
        Vulkan->ImageSamplerLinearNoMip = VK_NULL_HANDLE;
    }

    if (Vulkan->ImageSamplerNearestNoMip)
    {
        vkDestroySampler(Vulkan->Device, Vulkan->ImageSamplerNearestNoMip, nullptr);
        Vulkan->ImageSamplerNearestNoMip = VK_NULL_HANDLE;
    }

    if (Vulkan->ImageSamplerLinear)
    {
        vkDestroySampler(Vulkan->Device, Vulkan->ImageSamplerLinear, nullptr);
        Vulkan->ImageSamplerLinear = VK_NULL_HANDLE;
    }

    if (Vulkan->RenderPass)
    {
        vkDestroyRenderPass(Vulkan->Device, Vulkan->RenderPass, nullptr);
        Vulkan->RenderPass = VK_NULL_HANDLE;
    }

    if (Vulkan->DescriptorPool)
    {
        vkDestroyDescriptorPool(Vulkan->Device, Vulkan->DescriptorPool, nullptr);
        Vulkan->DescriptorPool = VK_NULL_HANDLE;
    }

    if (Vulkan->GraphicsCommandPool)
    {
        vkDestroyCommandPool(Vulkan->Device, Vulkan->GraphicsCommandPool, nullptr);
        Vulkan->GraphicsCommandPool = VK_NULL_HANDLE;
    }

    if (Vulkan->ComputeCommandPool)
    {
        vkDestroyCommandPool(Vulkan->Device, Vulkan->ComputeCommandPool, nullptr);
        Vulkan->ComputeCommandPool = VK_NULL_HANDLE;
    }

    if (Vulkan->Device)
    {
        vkDestroyDevice(Vulkan->Device, nullptr);
        Vulkan->Device = nullptr;
        Vulkan->GraphicsQueue = VK_NULL_HANDLE;
        Vulkan->ComputeQueue = VK_NULL_HANDLE;
        Vulkan->PresentQueue = VK_NULL_HANDLE;
    }

    if (Vulkan->PhysicalDevice)
    {
        Vulkan->PhysicalDevice = VK_NULL_HANDLE;
        Vulkan->PhysicalDeviceFeatures = {};
        Vulkan->PhysicalDeviceProperties = {};
        Vulkan->GraphicsQueueFamilyIndex = 0;
        Vulkan->ComputeQueueFamilyIndex = 0;
        Vulkan->PresentQueueFamilyIndex = 0;
        Vulkan->SurfaceFormat = {};
        Vulkan->PresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    if (Vulkan->Messenger)
    {
        DestroyDebugUtilsMessengerEXT(Vulkan->Instance, Vulkan->Messenger, nullptr);
        Vulkan->Messenger = VK_NULL_HANDLE;
    }

    if (Vulkan->Surface)
    {
        vkDestroySurfaceKHR(Vulkan->Instance, Vulkan->Surface, nullptr);
        Vulkan->Surface = VK_NULL_HANDLE;
        Vulkan->Window = nullptr;
    }

    if (Vulkan->Instance)
    {
        vkDestroyInstance(Vulkan->Instance, nullptr);
        Vulkan->Instance = VK_NULL_HANDLE;
    }
}

static void InternalWaitForWindowSize(vulkan* Vulkan)
{
    int Width = 0, Height = 0;
    glfwGetFramebufferSize(Vulkan->Window, &Width, &Height);
    while (Width == 0 || Height == 0)
    {
        glfwGetFramebufferSize(Vulkan->Window, &Width, &Height);
        glfwWaitEvents();
    }
}

VkResult BeginVulkanFrame(vulkan* Vulkan)
{
    assert(!Vulkan->CurrentFrame);

    VkResult Result = VK_SUCCESS;

    Vulkan->FrameIndex++;

    vulkan_frame* Frame = &Vulkan->FrameStates[Vulkan->FrameIndex % 2];

    uint32_t RandomSeed = Vulkan->FrameIndex * 65537;

    // Wait for the previous commands using this frame state to finish executing.
    vkWaitForFences(Vulkan->Device, 1, &Frame->AvailableFence, VK_TRUE, UINT64_MAX);

    // Try to acquire a swap chain image for us to render to.
    Result = vkAcquireNextImageKHR(Vulkan->Device, Vulkan->SwapChain, UINT64_MAX, Frame->ImageAvailableSemaphore, VK_NULL_HANDLE, &Frame->ImageIndex);

    if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR)
    {
        InternalWaitForWindowSize(Vulkan);
        vkDeviceWaitIdle(Vulkan->Device);
        InternalDestroyPresentationResources(Vulkan);
        InternalCreatePresentationResources(Vulkan);
        Result = vkAcquireNextImageKHR(Vulkan->Device, Vulkan->SwapChain, UINT64_MAX, Frame->ImageAvailableSemaphore, VK_NULL_HANDLE, &Frame->ImageIndex);
    }

    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to acquire swap chain image");
        return Result;
    }

    // Reset the fence to indicate that the frame state is no longer available.
    vkResetFences(Vulkan->Device, 1, &Frame->AvailableFence);

    // Prepare compute command buffer.
    vkResetCommandBuffer(Frame->ComputeCommandBuffer, 0);
    auto ComputeBeginInfo = VkCommandBufferBeginInfo
    {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags            = 0,
        .pInheritanceInfo = nullptr,
    };
    Result = vkBeginCommandBuffer(Frame->ComputeCommandBuffer, &ComputeBeginInfo);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to begin recording compute command buffer");
        return Result;
    }

    // Prepare graphics command buffer.
    vkResetCommandBuffer(Frame->GraphicsCommandBuffer, 0);
    auto GraphicsBeginInfo = VkCommandBufferBeginInfo
    {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags            = 0,
        .pInheritanceInfo = nullptr,
    };
    Result = vkBeginCommandBuffer(Frame->GraphicsCommandBuffer, &GraphicsBeginInfo);
    if (Result != VK_SUCCESS)
    {
        Errorf(Vulkan, "failed to begin recording graphics command buffer");
        return Result;
    }

    for (VkImage Image : Vulkan->SharedImages)
    {
        auto SubresourceRange = VkImageSubresourceRange
        {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        auto Barrier = VkImageMemoryBarrier
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = Image,
            .subresourceRange    = SubresourceRange,
        };

        vkCmdPipelineBarrier
        (
            Frame->GraphicsCommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &Barrier
        );
    }

    // Begin render pass.
    {
        VkClearValue ClearValues[] =
        {
            { .color = {{ 0.0f, 0.0f, 0.0f, 1.0f }} },
            { .depthStencil = { 1.0f, 0 } }
        };

        auto RenderPassBeginInfo = VkRenderPassBeginInfo
        {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = Vulkan->RenderPass,
            .framebuffer     = Vulkan->SwapChainFrameBuffers[Frame->ImageIndex],
            .renderArea      = { .offset = { 0, 0 }, .extent = Vulkan->SwapChainExtent },
            .clearValueCount = 2,
            .pClearValues    = ClearValues,
        };

        vkCmdBeginRenderPass(Frame->GraphicsCommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    Vulkan->CurrentFrame = Frame;

    return VK_SUCCESS;
}

VkResult EndVulkanFrame(vulkan* Vulkan)
{
    assert(Vulkan->CurrentFrame);

    vulkan_frame* Frame = Vulkan->CurrentFrame;

    VkResult Result = VK_SUCCESS;

    // Finish and submit compute command buffer.
    {
        Result = vkEndCommandBuffer(Frame->ComputeCommandBuffer);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to end recording compute command buffer");
            return Result;
        }

        VkSemaphore ComputeSignalSemaphores[] =
        {
            Frame->ComputeToComputeSemaphore,
            Frame->ComputeToGraphicsSemaphore,
        };

        VkPipelineStageFlags ComputeWaitStages[] =
        {
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        };

        auto ComputeSubmitInfo = VkSubmitInfo
        {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = Frame->Previous->Fresh ? 0u : 1u,
            .pWaitSemaphores      = &Frame->Previous->ComputeToComputeSemaphore,
            .pWaitDstStageMask    = ComputeWaitStages,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &Frame->ComputeCommandBuffer,
            .signalSemaphoreCount = static_cast<uint32_t>(std::size(ComputeSignalSemaphores)),
            .pSignalSemaphores    = ComputeSignalSemaphores,
        };

        Result = vkQueueSubmit(Vulkan->ComputeQueue, 1, &ComputeSubmitInfo, nullptr);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to submit compute command buffer");
            return Result;
        }
    }

    // Finish and submit graphics command buffer.
    {
        vkCmdEndRenderPass(Frame->GraphicsCommandBuffer);

        for (VkImage Image : Vulkan->SharedImages)
        {
            auto SubresourceRange = VkImageSubresourceRange
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            };

            auto Barrier = VkImageMemoryBarrier
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = Image,
                .subresourceRange    = SubresourceRange,
            };

            vkCmdPipelineBarrier
            (
                Frame->GraphicsCommandBuffer,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &Barrier
            );
        }

        Result = vkEndCommandBuffer(Frame->GraphicsCommandBuffer);
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to end recording graphics command buffer");
            return Result;
        }

        VkPipelineStageFlags GraphicsWaitStages[] =
        {
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        VkSemaphore GraphicsWaitSemaphores[] =
        {
            Frame->ComputeToGraphicsSemaphore,
            Frame->ImageAvailableSemaphore,
        };

        auto GraphicsSubmitInfo = VkSubmitInfo
        {
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
        if (Result != VK_SUCCESS)
        {
            Errorf(Vulkan, "failed to submit graphics command buffer");
            return Result;
        }

        auto PresentInfo = VkPresentInfoKHR
        {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &Frame->ImageFinishedSemaphore,
            .swapchainCount     = 1,
            .pSwapchains        = &Vulkan->SwapChain,
            .pImageIndices      = &Frame->ImageIndex,
            .pResults           = nullptr,
        };

        Result = vkQueuePresentKHR(Vulkan->PresentQueue, &PresentInfo);
        if (Result != VK_SUCCESS)
        {
            if (Result != VK_ERROR_OUT_OF_DATE_KHR && Result != VK_SUBOPTIMAL_KHR)
                Errorf(Vulkan, "failed to present swap chain image");
            return Result;
        }
    }

    Frame->Fresh = false;

    Vulkan->CurrentFrame = nullptr;

    return VK_SUCCESS;
}
