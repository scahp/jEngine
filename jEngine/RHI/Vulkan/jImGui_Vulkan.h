#pragma once

#include "Math/Vector.h"
#include "RHI/Vulkan/jBuffer_Vulkan.h"
#include "ImGui/jImGui.h"

struct jPushConstant;

// This is modifed from here https://github.com/SaschaWillems/Vulkan/blob/master/examples/imgui/main.cpp

class jImGUI_Vulkan : public jImGUI
{
public:
    jImGUI_Vulkan();
    virtual ~jImGUI_Vulkan();

    // UI params are set via push constants
    struct PushConstBlock {
        Vector2 scale;
        Vector2 translate;
    } pushConstBlock;

    // Initialize styles, keys, etc.
    virtual void Initialize(float width, float height) override;
    virtual void Release() override;

    // Draw current imGui frame into a command buffer
    virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr);

    // Todo : remove this for generalization of ui datas
    robin_hood::unordered_map<jName, uint64, jNameHashFunc> CounterMap;

private:
    virtual void NewFrameInternal() override;

    // Update vertex and index buffer containing the imGui elements when required
    void UpdateBuffers();

    // Initialize all Vulkan resources used by the ui
    jPipelineStateInfo* CreatePipelineState(jRenderPass* renderPass, VkQueue copyQueue);

private:
    // Vulkan resources for rendering the UI
    struct jDynamicBufferData
    {
        void Initialize()
        {
            VertexBufferPtr = std::make_shared<jBuffer_Vulkan>();
            IndexBufferPtr = std::make_shared<jBuffer_Vulkan>();
        }

        std::shared_ptr<jBuffer_Vulkan> VertexBufferPtr;
        std::shared_ptr<jBuffer_Vulkan> IndexBufferPtr;

        int32_t vertexCount = 0;
        int32_t indexCount = 0;
    };
    std::vector<jDynamicBufferData> DynamicBufferData;
    jTexture* FontImage = nullptr;

    // VkPipelineLayout PipelineLayout;
    VkDescriptorPool DescriptorPool;
    VkDescriptorSet DescriptorSet;
    jVertexBuffer* EmptyVertexBuffer = nullptr;
    jShaderBindingsLayout* EmptyShaderBindingLayout = nullptr;
    std::shared_ptr<jPushConstant> PushConstBlockPtr;

    bool IsInitialized = false;
};