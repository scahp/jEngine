#pragma once

#include "Math/Vector.h"
#include "RHI/Vulkan/jBuffer_Vulkan.h"

struct jPushConstant;

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

    static void ReleaseInstance()
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
    void Release();

    // Draw current imGui frame into a command buffer
    void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr);

    // Todo : remove this for generalization of ui datas
    robin_hood::unordered_map<jName, uint64, jNameHashFunc> CounterMap;

private:
    // Starts a new imGui frame and sets up windows and ui elements
    void NewFrame(bool updateFrameGraph);

    // Update vertex and index buffer containing the imGui elements when required
    void UpdateBuffers();

    // Initialize all Vulkan resources used by the ui
    jPipelineStateInfo* CreatePipelineState(jRenderPass* renderPass, VkQueue copyQueue);

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