#include "core/common.hpp"
#include "core/vulkan.hpp"
#include "scene/scene.hpp"
#include "application/imgui_render.hpp"

#include <imgui.h>

uint32_t const IMGUI_VERTEX_SHADER[] =
{
    #include "imgui_render.vertex.inc"
};

uint32_t const IMGUI_FRAGMENT_SHADER[] =
{
    #include "imgui_render.fragment.inc"
};

struct imgui_push_constant_buffer
{
    mat4 ProjectionMatrix = {};
    uint TextureID;
};

void CreateImGuiRenderContext
(
    vulkan*               Vulkan,
    vulkan_scene*         Scene,
    imgui_render_context* Context
)
{
    Context->Scene = Scene;

    VkDescriptorType DescriptorTypes[] =
    {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    };

    VkResult Result = CreateVulkanDescriptorSetLayout(Vulkan, &Context->DescriptorSetLayout, DescriptorTypes);
    if (Result != VK_SUCCESS) return;

    ImGuiIO& IO = ImGui::GetIO();
    unsigned char* Data;
    int Width, Height;
    IO.Fonts->GetTexDataAsRGBA32(&Data, &Width, &Height);
    size_t Size = Width * Height * sizeof(uint32_t);

    CreateVulkanImage
    (
        Vulkan,
        &Context->Texture,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8G8B8A8_SRGB,
        { .width = (uint32_t)Width, .height = (uint32_t)Height, .depth = 1},
        0,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        false
    );

    WriteToVulkanImage
    (
        Vulkan,
        &Context->Texture,
        0, 1,
        Data, (uint32_t)Width, (uint32_t)Height, sizeof(uint32_t),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    auto ImguiConfig = vulkan_graphics_pipeline_configuration
    {
        .VertexSize = sizeof(ImDrawVert),
        .VertexFormat =
        {
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
        .VertexShaderCode = IMGUI_VERTEX_SHADER,
        .FragmentShaderCode = IMGUI_FRAGMENT_SHADER,
        .DescriptorSetLayouts =
        {
            Context->DescriptorSetLayout,
            Scene->DescriptorSetLayout,
        },
        .PushConstantBufferSize = sizeof(imgui_push_constant_buffer),
    };

    Result = CreateVulkanGraphicsPipeline(Vulkan, &Context->Pipeline, ImguiConfig);
    if (Result != VK_SUCCESS) return;

    for (uint I = 0; I < 2; I++)
    {
        CreateVulkanBuffer
        (
            Vulkan,
            &Context->VertexBuffer[I],
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            65536 * sizeof(ImDrawVert)
        );

        CreateVulkanBuffer
        (
            Vulkan,
            &Context->IndexBuffer[I],
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            65536 * sizeof(uint16_t)
        );

        // ImGui descriptor set.
        vulkan_descriptor ImguiDescriptors[] =
        {
            {
                .Type        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .Image       = &Context->Texture,
                .ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .Sampler     = Vulkan->ImageSamplerLinear
            }
        };

        VkResult Result = CreateVulkanDescriptorSet
        (
            Vulkan,
            Context->DescriptorSetLayout,
            &Context->DescriptorSet[I],
            ImguiDescriptors
        );
        if (Result != VK_SUCCESS) return;
    }
}

void DestroyImGuiRenderContext
(
    vulkan*               Vulkan,
    imgui_render_context* Context
)
{
    DestroyVulkanImage(Vulkan, &Context->Texture);

    DestroyVulkanPipeline(Vulkan, &Context->Pipeline);

    for (uint I = 0; I < 2; I++)
    {
        DestroyVulkanBuffer(Vulkan, &Context->IndexBuffer[I]);
        DestroyVulkanBuffer(Vulkan, &Context->VertexBuffer[I]);
    }

    vkDestroyDescriptorSetLayout(Vulkan->Device, Context->DescriptorSetLayout, nullptr);
}

void RenderImGui
(
    vulkan*                 Vulkan,
    imgui_render_context*   Context
)
{
    ImGui::Render();

    auto DrawData = ImGui::GetDrawData();

    auto Scene = Context->Scene;
    auto Frame = Vulkan->CurrentFrame;

    auto IndexBuffer = &Context->IndexBuffer[Frame->Index];
    auto VertexBuffer = &Context->VertexBuffer[Frame->Index];
    auto DescriptorSet = Context->DescriptorSet[Frame->Index];

    auto Viewport = VkViewport
    {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>(Vulkan->SwapChainExtent.width),
        .height   = static_cast<float>(Vulkan->SwapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    // Upload vertex and index data.
    void* VertexMemory;
    uint32_t VertexOffset = 0;
    vkMapMemory(Vulkan->Device, VertexBuffer->Memory, 0, VertexBuffer->Size, 0, &VertexMemory);
    void* IndexMemory;
    uint32_t IndexOffset = 0;
    vkMapMemory(Vulkan->Device, IndexBuffer->Memory, 0, IndexBuffer->Size, 0, &IndexMemory);

    ImDrawVert* VertexPointer = static_cast<ImDrawVert*>(VertexMemory);
    uint16_t* IndexPointer = static_cast<uint16_t*>(IndexMemory);

    for (int I = 0; I < DrawData->CmdListsCount; I++)
    {
        ImDrawList* CmdList = DrawData->CmdLists[I];

        uint32_t VertexDataSize = CmdList->VtxBuffer.Size * sizeof(ImDrawVert);
        memcpy(VertexPointer, CmdList->VtxBuffer.Data, VertexDataSize);
        VertexPointer += CmdList->VtxBuffer.Size;

        uint32_t IndexDataSize = CmdList->IdxBuffer.Size * sizeof(uint16_t);
        memcpy(IndexPointer, CmdList->IdxBuffer.Data, IndexDataSize);
        IndexPointer += CmdList->IdxBuffer.Size;
    }

    vkUnmapMemory(Vulkan->Device, IndexBuffer->Memory);
    vkUnmapMemory(Vulkan->Device, VertexBuffer->Memory);

    vkCmdBindPipeline
    (
        Frame->GraphicsCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        Context->Pipeline.Pipeline
    );

    VkDescriptorSet DescriptorSets[] =
    {
        DescriptorSet,
        Scene->DescriptorSet,
    };

    vkCmdBindDescriptorSets
    (
        Frame->GraphicsCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        Context->Pipeline.PipelineLayout,
        0, 2, DescriptorSets,
        0, nullptr
    );

    VkDeviceSize Offset = 0;
    vkCmdBindVertexBuffers
    (
        Frame->GraphicsCommandBuffer,
        0, 1, &VertexBuffer->Buffer, &Offset
    );

    vkCmdBindIndexBuffer
    (
        Frame->GraphicsCommandBuffer,
        IndexBuffer->Buffer,
        0, VK_INDEX_TYPE_UINT16
    );

    vkCmdSetViewport(Frame->GraphicsCommandBuffer, 0, 1, &Viewport);

    uint32_t IndexBase = 0;
    uint32_t VertexBase = 0;

    imgui_push_constant_buffer PushConstantBuffer = {};

    float L = DrawData->DisplayPos.x;
    float R = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
    float T = DrawData->DisplayPos.y;
    float B = DrawData->DisplayPos.y + DrawData->DisplaySize.y;

    PushConstantBuffer.ProjectionMatrix =
    {
        { 2.0f / (R - L),    0.0f,              0.0f, 0.0f },
        { 0.0f,              2.0f / (B - T),    0.0f, 0.0f },
        { 0.0f,              0.0f,              0.5f, 0.0f },
        { (R + L) / (L - R), (T + B) / (T - B), 0.5f, 1.0f },
    };

    PushConstantBuffer.TextureID = 0xFFFFFFFF;

    for (int I = 0; I < DrawData->CmdListsCount; I++)
    {
        ImDrawList* CmdList = DrawData->CmdLists[I];

        for (int j = 0; j < CmdList->CmdBuffer.Size; j++)
        {
            ImDrawCmd* Cmd = &CmdList->CmdBuffer[j];

            int32_t X0 = static_cast<int32_t>(Cmd->ClipRect.x - DrawData->DisplayPos.x);
            int32_t Y0 = static_cast<int32_t>(Cmd->ClipRect.y - DrawData->DisplayPos.y);
            int32_t X1 = static_cast<int32_t>(Cmd->ClipRect.z - DrawData->DisplayPos.x);
            int32_t Y1 = static_cast<int32_t>(Cmd->ClipRect.w - DrawData->DisplayPos.y);

            auto scissor = VkRect2D
            {
                .offset = { X0, Y0 },
                .extent = { static_cast<uint32_t>(X1 - X0), static_cast<uint32_t>(Y1 - Y0) },
            };
            vkCmdSetScissor(Frame->GraphicsCommandBuffer, 0, 1, &scissor);

            uint32_t TextureID = static_cast<uint32_t>(reinterpret_cast<size_t>(Cmd->TextureId));
            if (TextureID != PushConstantBuffer.TextureID)
            {
                PushConstantBuffer.TextureID = TextureID;
                vkCmdPushConstants
                (
                    Frame->GraphicsCommandBuffer,
                    Context->Pipeline.PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(imgui_push_constant_buffer), &PushConstantBuffer
                );
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
