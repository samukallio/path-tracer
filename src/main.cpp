#include <stdio.h>

#include "vulkan.h"
#include "scene.h"

#include <GLFW/glfw3.h>

int const WINDOW_WIDTH = 1024;
int const WINDOW_HEIGHT = 1024;
char const* APPLICATION_NAME = "Path Tracer";

int main()
{
    Scene scene;

    LoadScene(&scene, "../armadillo.obj");

    for (MeshNode const& node : scene.meshNodes) {
        if (node.faceEndIndex > 0) {
            assert(node.faceBeginOrNodeIndex <= scene.meshFaces.size());
            assert(node.faceEndIndex <= scene.meshFaces.size());
        }
        else {
            assert(node.faceBeginOrNodeIndex < scene.meshNodes.size());
        }
    }

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    auto window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);

    auto vulkan = CreateVulkan(window, APPLICATION_NAME);

    UploadSceneGeometry(vulkan, &scene);

    glm::vec3 viewPosition = { 0, 0, 0 };
    glm::vec3 viewVelocity = { 0, 0, 0 };
    glm::vec3 viewDirection = { 0, 0, -1 };
    glm::f32 viewPitch = 0.0f;
    glm::f32 viewYaw = 0.0f;

    double previousTime = glfwGetTime();
    double previousMouseX;
    double previousMouseY;
    glfwGetCursorPos(window, &previousMouseX, &previousMouseY);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Calculate deltaTime.
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        // Calculate mouse movement.
        double currentMouseX, currentMouseY;
        glfwGetCursorPos(window, &currentMouseX, &currentMouseY);
        float deltaMouseX = static_cast<float>(currentMouseX - previousMouseX);
        float deltaMouseY = static_cast<float>(currentMouseY - previousMouseY);
        previousMouseX = currentMouseX;
        previousMouseY = currentMouseY;

        glm::vec3 viewMove {};
        if (glfwGetKey(window, GLFW_KEY_A))
            viewMove -= glm::cross(viewDirection, glm::vec3(0, 1, 0));
        if (glfwGetKey(window, GLFW_KEY_D))
            viewMove += glm::cross(viewDirection, glm::vec3(0, 1, 0));
        if (glfwGetKey(window, GLFW_KEY_W))
            viewMove += viewDirection;
        if (glfwGetKey(window, GLFW_KEY_S))
            viewMove -= viewDirection;
        if (glm::length(viewMove) > 0)
            viewVelocity = 2.0f * glm::normalize(viewMove);
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1)) {
            viewYaw -= deltaMouseX * 0.01f;
            viewPitch += deltaMouseY * 0.01f;
            viewPitch = glm::clamp(viewPitch, -glm::pi<glm::f32>() * 0.45f, +glm::pi<glm::f32>() * 0.45f);
        }

        viewDirection = glm::quat(glm::vec3(viewPitch, viewYaw, 0)) * glm::vec3(0, 0, 1);
        viewPosition += deltaTime * viewVelocity;
        viewVelocity *= expf(-deltaTime / 0.25f);

        glm::mat4 viewMatrix = glm::lookAt(viewPosition - viewDirection * 2.0f, viewPosition, glm::vec3(0, 1, 0));

        SceneUniformBuffer parameters = {
            .viewMatrixInverse = glm::inverse(viewMatrix),
            .frameIndex = vulkan->frameIndex,
            .color = glm::vec4(1, 0, 1, 0),
        };
        RenderFrame(vulkan, &parameters);

        previousTime = currentTime;
    }

    DestroyVulkan(vulkan);

    glfwDestroyWindow(window);
    glfwTerminate();
}