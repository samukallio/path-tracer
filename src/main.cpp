#include <stdio.h>

#include "vulkan.h"

#include <GLFW/glfw3.h>

int const WINDOW_WIDTH = 1024;
int const WINDOW_HEIGHT = 768;
char const* APPLICATION_NAME = "Path Tracer";

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    auto window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);

    auto vulkan = CreateVulkan(window, APPLICATION_NAME);



    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        //VkResult result = VK_SUCCESS;
        //VulkanFrameState* frame = nullptr;

        //result = BeginFrame(vulkan, &frame);
        //if (result != VK_SUCCESS)
        //    break;

        //VulkanDescriptor renderDescriptors[] = {
        //    {
        //        .image = {
        //            .sampler = VK_NULL_HANDLE,
        //            .imageView = renderTargetImage->view,
        //            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        //        }
        //    }
        //};
        //UpdateVulkanPipelineDescriptors(vulkan, frame, renderPipeline, renderDescriptors);

        //BindVulkanPipeline(vulkan, frame, renderPipeline);
        //uint16_t groupCountX = 512 / 16;
        //uint16_t groupCountY = 512 / 16;
        //vkCmdDispatch(frame->computeCommandBuffer, groupCountX, groupCountY, 1);

        //BindVulkanPipeline(vulkan, frame, blitPipeline);
        //vkCmdDraw(frame->graphicsCommandBuffer, 6, 1, 0, 0);

        //EndFrame(vulkan, frame);

        RenderFrame(vulkan);
    }

    DestroyVulkan(vulkan);

    glfwDestroyWindow(window);
    glfwTerminate();
}