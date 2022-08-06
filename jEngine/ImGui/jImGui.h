#pragma once

#include "Math/Vector.h"
#include "RHI/Vulkan/jBuffer_Vulkan.h"

// This is modifed from here https://github.com/SaschaWillems/Vulkan/blob/master/examples/imgui/main.cpp

class jImGUI_Vulkan final
{
public:
    static jImGUI_Vulkan& Get()
    {
        if (!s_instance)
            s_instance = new jImGUI_Vulkan();

        return *s_instance;
    }

    static void Release()
    {
        delete s_instance;
    }

    // UI params are set via push constants
    struct PushConstBlock {
        Vector2 scale;
        Vector2 translate;
    } pushConstBlock;

    // Initialize styles, keys, etc.
    void Initialize(float width, float height);

    void Update(float InDeltaSeconds);

    void PrepareDraw(int32 imageIndex);

    // Draw current imGui frame into a command buffer
    void Draw(jCommandBuffer* commandBuffer, int32 imageIndex);

private:
    // Starts a new imGui frame and sets up windows and ui elements
    void NewFrame(bool updateFrameGraph);

    // Update vertex and index buffer containing the imGui elements when required
    void UpdateBuffers();

    // Initialize all Vulkan resources used by the ui
    VkPipeline CreatePipelineState(VkRenderPass renderPass, VkQueue copyQueue);

private:
    // Singletone variables and functions
    jImGUI_Vulkan();
    ~jImGUI_Vulkan();

    jImGUI_Vulkan(const jImGUI_Vulkan&) = delete;
    jImGUI_Vulkan& operator=(const jImGUI_Vulkan&) = delete;

    static jImGUI_Vulkan* s_instance;

private:
    // Vulkan resources for rendering the UI
    struct jDynamicBufferData
    {
        jBuffer_Vulkan VertexBuffer;
        jBuffer_Vulkan IndexBuffer;

        int32_t vertexCount = 0;
        int32_t indexCount = 0;
    };
    std::vector<jDynamicBufferData> DynamicBufferData;
    jTexture* FontImage = nullptr;

    VkPipelineLayout PipelineLayout;
    VkDescriptorPool DescriptorPool;
    VkDescriptorSetLayout DescriptorSetLayout;
    VkDescriptorSet DescriptorSet;
    std::vector<jRenderPass*> RenderPasses;
    std::vector<VkPipeline> Pipelines;
};