#include <stdio.h>

#include "vulkan.h"

#include <GLFW/glfw3.h>

int const WINDOW_WIDTH = 1024;
int const WINDOW_HEIGHT = 768;
char const* APPLICATION_NAME = "Path Tracer";

uint32_t const BLIT_VERTEX_SHADER[] =
{
    #include "blit.vertex.inc"
};

uint32_t const BLIT_FRAGMENT_SHADER[] =
{
    #include "blit.fragment.inc"
};


int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    auto window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);

    auto vulkan = CreateVulkan(window, APPLICATION_NAME);

    auto renderTargetImage = CreateVulkanImage(vulkan,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8G8B8A8_UNORM,
        { .width = 512, .height = 512, .depth = 1 },
        VK_IMAGE_TILING_OPTIMAL);

    VulkanGraphicsPipelineConfiguration blitConfig = {
        .vertexSize = 0,
        .vertexFormat = {},
        .vertexShaderCode = BLIT_VERTEX_SHADER,
        .fragmentShaderCode = BLIT_FRAGMENT_SHADER,
        .descriptorTypes = {},
    };

    auto blitPipeline = CreateVulkanGraphicsPipeline(vulkan, blitConfig);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        VkResult result = VK_SUCCESS;
        VulkanFrameState* frame = nullptr;

        result = BeginFrame(vulkan, &frame);
        if (result != VK_SUCCESS)
            break;

        BindVulkanPipeline(vulkan, frame, blitPipeline);
        vkCmdDraw(frame->graphicsCommandBuffer, 6, 1, 0, 0);

        EndFrame(vulkan, frame);
    }

    vkDeviceWaitIdle(vulkan->device);

    DestroyVulkanPipeline(vulkan, blitPipeline);

    DestroyVulkanImage(vulkan, renderTargetImage);

    DestroyVulkan(vulkan);

    glfwDestroyWindow(window);
    glfwTerminate();
}