#include <stdio.h>

#include "vulkan.h"
#include "scene.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

int const WINDOW_WIDTH = 1920;
int const WINDOW_HEIGHT = 1080;
char const* APPLICATION_NAME = "Path Tracer";

int main()
{
    Scene scene;

    LoadMesh(&scene, "../scene/sponza.obj", 0.01f);
    LoadSkybox(&scene, "../scene/MegaSun4k.hdr");
    AddMesh(&scene, glm::vec3(0, 0, 0), 0);
    //AddPlane(&scene, glm::vec3(0, 0, -1.1));

    uint32_t lightMaterialIndex = (uint32_t)scene.materials.size();
    scene.materials.push_back({
        .albedoColor = glm::vec3(1, 1, 1),
        .albedoTextureIndex = 0,
        .specularColor = glm::vec4(1, 1, 1, 0),
        .emissiveColor = 150.0f * glm::vec3(1,243/255.0f,142/255.0f),
        .emissiveTextureIndex = 0,
        .roughness = 1.0f,
        .specularProbability = 0.0f,
        .refractProbability = 0.0f,
        .refractIndex = 0.0f,
        .albedoTextureSize = glm::uvec2(0, 0),
        .padding = glm::uvec2(0, 0),
    });
    scene.objects.push_back({
        .origin = glm::vec3(0, 0, 5),
        .type = OBJECT_SPHERE,
        .scale = 0.25f * glm::vec3(1, 1, 1),
        .materialIndex = lightMaterialIndex,
    });
 
    for (MeshNode const& node : scene.meshNodes) {
        if (node.faceEndIndex > 0) {
            assert(node.faceBeginOrNodeIndex <= scene.meshFaces.size());
            assert(node.faceEndIndex <= scene.meshFaces.size());
        }
        else {
            assert(node.faceBeginOrNodeIndex < scene.meshNodes.size());
        }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    auto window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);

    auto vulkan = CreateVulkan(window, APPLICATION_NAME);

    UploadScene(vulkan, &scene);

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

        io.MousePos.x = currentMouseX;
        io.MousePos.y = currentMouseY;
        io.MouseDown[0] = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1);

        bool clearFrame = false;

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_2)) {
            glm::vec3 viewMove {};
            if (glfwGetKey(window, GLFW_KEY_A))
                viewMove -= glm::cross(viewDirection, glm::vec3(0, 0, 1));
            if (glfwGetKey(window, GLFW_KEY_D))
                viewMove += glm::cross(viewDirection, glm::vec3(0, 0, 1));
            if (glfwGetKey(window, GLFW_KEY_W))
                viewMove += viewDirection;
            if (glfwGetKey(window, GLFW_KEY_S))
                viewMove -= viewDirection;
            if (glm::length(viewMove) > 0)
                viewVelocity = 2.0f * glm::normalize(viewMove);

            viewYaw -= deltaMouseX * 0.01f;
            viewPitch += deltaMouseY * 0.01f;
            viewPitch = glm::clamp(viewPitch, -glm::pi<glm::f32>() * 0.45f, +glm::pi<glm::f32>() * 0.45f);
            clearFrame = true;
        }

        viewDirection = glm::quat(glm::vec3(0, viewPitch, viewYaw)) * glm::vec3(1, 0, 0);
        viewPosition += deltaTime * viewVelocity;
        viewVelocity *= expf(-deltaTime / 0.05f);

        if (glm::length(viewVelocity) < 1e-2f)
            viewVelocity = glm::vec3(0);
        else
            clearFrame = true;

        glm::mat4 viewMatrix = glm::lookAt(viewPosition - viewDirection * 2.0f, viewPosition, glm::vec3(0, 0, 1));

        // ImGui.
        io.DisplaySize.x = WINDOW_WIDTH;
        io.DisplaySize.y = WINDOW_HEIGHT;
        io.DeltaTime = deltaTime;

        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Statistics", nullptr,
            ImGuiWindowFlags_NoBackground|
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoMove);
        ImGui::Text("Boo");
        ImGui::End();

        ImGui::ShowDemoWindow();

        ImGui::EndFrame();
        ImGui::Render();


        // Rendering.
        SceneUniformBuffer parameters = {
            .viewMatrixInverse = glm::inverse(viewMatrix),
            .nearPlaneSize = { 2, 2 * WINDOW_HEIGHT / float(WINDOW_WIDTH) },
            .frameIndex = vulkan->frameIndex,
            .objectCount = static_cast<uint32_t>(scene.objects.size()),
            .clearFrame = clearFrame,
        };
        RenderFrame(vulkan, &parameters, ImGui::GetDrawData());

        previousTime = currentTime;
    }

    DestroyVulkan(vulkan);

    glfwDestroyWindow(window);
    glfwTerminate();

    ImGui::DestroyContext();
}