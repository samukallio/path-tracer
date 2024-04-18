#include <stdio.h>

#include "vulkan.h"
#include "scene.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

int const WINDOW_WIDTH = 1920;
int const WINDOW_HEIGHT = 1080;
char const* APPLICATION_NAME = "Path Tracer";

struct CameraState
{
    glm::vec3       position;
    glm::vec3       velocity;
    glm::vec3       direction;
    float           pitch;
    float           yaw;
};

struct FrameState
{
    double          time;
    double          mouseX;
    double          mouseY;
};

struct AppContext
{
    GLFWwindow*     window = nullptr;
    VulkanContext*  vulkan = nullptr;
    Scene           scene;
    CameraState     camera;
    FrameState      frames[2];
    uint32_t        frameIndex = 0;

    bool            accumulateSamples;

    RenderMode      renderMode;
    ToneMapping     toneMapping;
    CameraType      cameraType;
    float           cameraFocalLengthInMM;
    float           cameraApertureRadiusInMM;
    float           cameraFocusDistance;
};

AppContext app;

void Frame()
{
    FrameState& previous = app.frames[(app.frameIndex + 0) % 2];
    FrameState& current = app.frames[(app.frameIndex + 1) % 2];
    app.frameIndex++;

    // Update time and input.
    current.time = glfwGetTime();
    float deltaTime = static_cast<float>(current.time - previous.time);
    glfwGetCursorPos(app.window, &current.mouseX, &current.mouseY);
    float deltaMouseX = static_cast<float>(current.mouseX - previous.mouseX);
    float deltaMouseY = static_cast<float>(current.mouseY - previous.mouseY);

    // ImGui.
    bool cameraChanged = false;
    {
        bool c = false;

        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = deltaTime;
        io.MousePos.x = static_cast<float>(current.mouseX);
        io.MousePos.y = static_cast<float>(current.mouseY);
        io.MouseDown[0] = glfwGetMouseButton(app.window, GLFW_MOUSE_BUTTON_1);
        io.DisplaySize.x = WINDOW_WIDTH;
        io.DisplaySize.y = WINDOW_HEIGHT;

        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::Begin("Controls");

        ImGui::SeparatorText("General");
        ImGui::Checkbox("Accumulate Samples", &app.accumulateSamples);

        ImGui::SeparatorText("Render Mode");
        c |= ImGui::RadioButton("Path Tracing", (int*)&app.renderMode, RENDER_MODE_PATH_TRACE);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Albedo", (int*)&app.renderMode, RENDER_MODE_ALBEDO);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Normal", (int*)&app.renderMode, RENDER_MODE_NORMAL);
        c |= ImGui::RadioButton("Material ID", (int*)&app.renderMode, RENDER_MODE_MATERIAL_INDEX);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Primitive ID", (int*)&app.renderMode, RENDER_MODE_PRIMITIVE_INDEX);

        // Tone mapping operators.  Note that since tone mapping happens as
        // a post-process operation, there is no need to reset the accumulated
        // samples.
        ImGui::SeparatorText("Tone Mapping");
        ImGui::RadioButton("Clamp", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_CLAMP);
        ImGui::SameLine();
        ImGui::RadioButton("Reinhard", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_REINHARD);
        ImGui::SameLine();
        ImGui::RadioButton("Hable", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_HABLE);
        ImGui::RadioButton("ACES", (int*)&app.toneMapping.mode, TONE_MAPPING_MODE_ACES);

        if (app.toneMapping.mode == TONE_MAPPING_MODE_REINHARD) {
            ImGui::SliderFloat("White Level", &app.toneMapping.whiteLevel, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        }

        ImGui::SeparatorText("Camera");
        c |= ImGui::RadioButton("Pinhole", (int*)&app.cameraType, CAMERA_TYPE_PINHOLE);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Thin Lens", (int*)&app.cameraType, CAMERA_TYPE_THIN_LENS);

        if (app.cameraType == CAMERA_TYPE_PINHOLE) {
        }

        if (app.cameraType == CAMERA_TYPE_THIN_LENS) {
            c |= ImGui::SliderFloat("Focal Length (mm)", &app.cameraFocalLengthInMM, 1.0f, 50.0f);
            c |= ImGui::SliderFloat("Aperture Radius (mm)", &app.cameraApertureRadiusInMM, 0.01f, 50.0f);
            c |= ImGui::SliderFloat("Focus Distance", &app.cameraFocusDistance, 0.01f, 1000.0f,  "%.3f", ImGuiSliderFlags_Logarithmic);
        }

        ImGui::End();
        ImGui::ShowDemoWindow();
        ImGui::EndFrame();
        ImGui::Render();

        cameraChanged = c;
    }

    // Handle camera movement.
    CameraState& camera = app.camera;
    bool cameraMoved = false;
    {
        if (glfwGetMouseButton(app.window, GLFW_MOUSE_BUTTON_2)) {
            glm::vec3 delta {};
            if (glfwGetKey(app.window, GLFW_KEY_A))
                delta -= glm::cross(camera.direction, glm::vec3(0, 0, 1));
            if (glfwGetKey(app.window, GLFW_KEY_D))
                delta += glm::cross(camera.direction, glm::vec3(0, 0, 1));
            if (glfwGetKey(app.window, GLFW_KEY_W))
                delta += camera.direction;
            if (glfwGetKey(app.window, GLFW_KEY_S))
                delta -= camera.direction;
            if (glm::length(delta) > 0)
                camera.velocity = 2.0f * glm::normalize(delta);

            camera.yaw -= deltaMouseX * 0.01f;
            camera.pitch += deltaMouseY * 0.01f;
            camera.pitch = glm::clamp(camera.pitch, -glm::pi<glm::f32>() * 0.45f, +glm::pi<glm::f32>() * 0.45f);
            cameraMoved = true;
        }

        camera.direction = glm::quat(glm::vec3(0, camera.pitch, camera.yaw)) * glm::vec3(1, 0, 0);
        camera.position += deltaTime * camera.velocity;
        camera.velocity *= expf(-deltaTime / 0.05f);

        if (glm::length(camera.velocity) > 0)
            cameraMoved = true;

        if (glm::length(camera.velocity) < 1e-2f)
            camera.velocity = glm::vec3(0);
    }

    glm::mat4 viewMatrix = glm::lookAt(camera.position - camera.direction * 2.0f, camera.position, glm::vec3(0, 0, 1));

    // Render frame.
    float sensorDistance = 1.0f / (1000.0f / app.cameraFocalLengthInMM - 1.0f / app.cameraFocusDistance);

    FrameUniformBuffer uniforms = {
        .frameIndex = app.frameIndex,
        .objectCount = static_cast<uint32_t>(app.scene.objects.size()),
        .clearFrame = cameraChanged || cameraMoved || !app.accumulateSamples,
        .renderMode = app.renderMode,
        .toneMapping = app.toneMapping,
        .camera = {
            .type = app.cameraType,
            .focalLength = app.cameraFocalLengthInMM / 1000.0f,
            .apertureRadius = app.cameraApertureRadiusInMM / 1000.0f,
            .sensorDistance = sensorDistance,
            .sensorSize = { 0.032f, 0.018f },
            .worldMatrix = glm::inverse(viewMatrix),
        },
    };
    RenderFrame(app.vulkan, &uniforms, ImGui::GetDrawData());
}

int main()
{
    //LoadMesh(&scene, "../scene/sponza.obj", 0.01f);
    Scene& scene = app.scene;
    LoadMesh(&scene, "../scene/viking_room.obj", 1.0f);
    AddTextureFromFile(&scene, "../scene/viking_room.png");
    AddTextureFromFile(&scene, "../scene/viking_room.png");
    LoadSkybox(&scene, "../scene/MegaSun4k.hdr");
    AddMesh(&scene, glm::vec3(0, 0, 0), 0);
    //AddPlane(&scene, glm::vec3(0, 0, -1.1));

    scene.materials.push_back({
        .albedoColor = glm::vec3(1, 1, 1),
        .albedoTextureIndex = 0,
        .specularColor = glm::vec4(1, 1, 1, 0),
        .emissiveColor = glm::vec3(0, 0, 0),
        .emissiveTextureIndex = 0,
        .roughness = 1.0f,
        .specularProbability = 0.0f,
        .refractProbability = 0.0f,
        .refractIndex = 0.0f,
        .albedoTextureSize = glm::uvec2(1024, 1024),
        .padding = glm::uvec2(0, 0),
    });

    uint32_t sphereMaterialIndex = (uint32_t)scene.materials.size();
    scene.materials.push_back({
        .albedoColor = glm::vec3(1, 1, 1),
        .albedoTextureIndex = 0,
        .specularColor = glm::vec4(1, 1, 1, 0),
        .emissiveColor = glm::vec3(0, 0, 0),
        .emissiveTextureIndex = 0,
        .roughness = 1.0f,
        .specularProbability = 0.0f,
        .refractProbability = 1.0f,
        .refractIndex = 1.5f,
        .albedoTextureSize = glm::uvec2(0, 0),
        .padding = glm::uvec2(0, 0),
    });
    scene.objects.push_back({
        .origin = glm::vec3(0.55f, 0.1f, 0.15f),
        .type = OBJECT_SPHERE,
        .scale = 0.15f * glm::vec3(1, 1, 1),
        .materialIndex = sphereMaterialIndex,
    });


    //uint32_t lightMaterialIndex = (uint32_t)scene.materials.size();
    //scene.materials.push_back({
    //    .albedoColor = glm::vec3(1, 1, 1),
    //    .albedoTextureIndex = 0,
    //    .specularColor = glm::vec4(1, 1, 1, 0),
    //    .emissiveColor = 150.0f * glm::vec3(1,243/255.0f,142/255.0f),
    //    .emissiveTextureIndex = 0,
    //    .roughness = 1.0f,
    //    .specularProbability = 0.0f,
    //    .refractProbability = 0.0f,
    //    .refractIndex = 0.0f,
    //    .albedoTextureSize = glm::uvec2(0, 0),
    //    .padding = glm::uvec2(0, 0),
    //});
    //scene.objects.push_back({
    //    .origin = glm::vec3(0, 0, 5),
    //    .type = OBJECT_SPHERE,
    //    .scale = 0.25f * glm::vec3(1, 1, 1),
    //    .materialIndex = lightMaterialIndex,
    //});
 
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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    app.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APPLICATION_NAME, nullptr, nullptr);
    app.vulkan = CreateVulkan(app.window, APPLICATION_NAME);

    UploadScene(app.vulkan, &scene);

    app.camera.position = { 0, 0, 0 };
    app.camera.velocity = { 0, 0, 0 };
    app.camera.direction = { 0, 0, -1 };
    app.camera.pitch = 0.0f;
    app.camera.yaw = 0.0f;

    app.cameraFocusDistance = 1.0f;
    app.cameraFocalLengthInMM = 20.0f;
    app.cameraApertureRadiusInMM = 40.0f;

    FrameState& initial = app.frames[0];
    initial.time = glfwGetTime();
    glfwGetCursorPos(app.window, &initial.mouseX, &initial.mouseY);

    while (!glfwWindowShouldClose(app.window)) {
        glfwPollEvents();
        Frame();
    }

    DestroyVulkan(app.vulkan);

    glfwDestroyWindow(app.window);
    glfwTerminate();

    ImGui::DestroyContext();
}
