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

        VkResult result = VK_SUCCESS;
        VulkanFrameState* frame = nullptr;

        result = BeginFrame(vulkan, &frame);
        if (result != VK_SUCCESS)
            break;

        EndFrame(vulkan, frame);
    }

    DestroyVulkan(vulkan);

    glfwDestroyWindow(window);
    glfwTerminate();
}