#pragma once

struct jBuffer_Vulkan;

struct jSamplerStateInfo_Vulkan : public jSamplerStateInfo
{
    jSamplerStateInfo_Vulkan() = default;
    jSamplerStateInfo_Vulkan(const jSamplerStateInfo& state) : jSamplerStateInfo(state) {}
    virtual ~jSamplerStateInfo_Vulkan() 
    {
        Release();
    }
    virtual void Initialize() override;
    void Release();

    virtual void* GetHandle() const { return SamplerState; }

    VkSamplerCreateInfo SamplerStateInfo = {};
    VkSampler SamplerState = nullptr;
};

struct jRasterizationStateInfo_Vulkan : public jRasterizationStateInfo
{
    jRasterizationStateInfo_Vulkan() = default;
    jRasterizationStateInfo_Vulkan(const jRasterizationStateInfo& state) : jRasterizationStateInfo(state) {}
    virtual ~jRasterizationStateInfo_Vulkan() {}
    virtual void Initialize() override;

    VkPipelineRasterizationStateCreateInfo RasterizationStateInfo = {};
    VkPipelineMultisampleStateCreateInfo MultisampleStateInfo = {};
};

//struct jMultisampleStateInfo_Vulkan : public jMultisampleStateInfo
//{
//    jMultisampleStateInfo_Vulkan() = default;
//    jMultisampleStateInfo_Vulkan(const jMultisampleStateInfo& state) : jMultisampleStateInfo(state) {}
//    virtual ~jMultisampleStateInfo_Vulkan() {}
//    virtual void Initialize() override;
//
//    VkPipelineMultisampleStateCreateInfo MultisampleStateInfo = {};
//};

struct jStencilOpStateInfo_Vulkan : public jStencilOpStateInfo
{
    jStencilOpStateInfo_Vulkan() = default;
    jStencilOpStateInfo_Vulkan(const jStencilOpStateInfo& state) : jStencilOpStateInfo(state) {}
    virtual ~jStencilOpStateInfo_Vulkan() {}
    virtual void Initialize() override;

    VkStencilOpState StencilOpStateInfo = {};
};

struct jDepthStencilStateInfo_Vulkan : public jDepthStencilStateInfo
{
    jDepthStencilStateInfo_Vulkan() = default;
    jDepthStencilStateInfo_Vulkan(const jDepthStencilStateInfo& state) : jDepthStencilStateInfo(state) {}
    virtual ~jDepthStencilStateInfo_Vulkan() {}
    virtual void Initialize() override;

    VkPipelineDepthStencilStateCreateInfo DepthStencilStateInfo = {};
};

struct jBlendingStateInfo_Vulkan : public jBlendingStateInfo
{
    jBlendingStateInfo_Vulkan() = default;
    jBlendingStateInfo_Vulkan(const jBlendingStateInfo& state) : jBlendingStateInfo(state) {}
    virtual ~jBlendingStateInfo_Vulkan() {}
    virtual void Initialize() override;

    VkPipelineColorBlendAttachmentState ColorBlendAttachmentInfo = {};
};

//////////////////////////////////////////////////////////////////////////
// jPipelineStateInfo_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jPipelineStateInfo_Vulkan : public jPipelineStateInfo
{
    jPipelineStateInfo_Vulkan() = default;
    jPipelineStateInfo_Vulkan(const jPipelineStateFixedInfo* pipelineStateFixed, const jGraphicsPipelineShader shader, const jVertexBufferArray& InVertexBufferArray
        , const jRenderPass* renderPass, const jShaderBindingLayoutArray& InShaderBindingLayoutArray, const jPushConstant* pushConstant)
        : jPipelineStateInfo(pipelineStateFixed, shader, InVertexBufferArray, renderPass, InShaderBindingLayoutArray, pushConstant)
    {}
    jPipelineStateInfo_Vulkan(const jPipelineStateInfo& pipelineState)
        : jPipelineStateInfo(pipelineState)
    {}
    jPipelineStateInfo_Vulkan(jPipelineStateInfo&& pipelineState)
        : jPipelineStateInfo(std::move(pipelineState))
    {}
    virtual ~jPipelineStateInfo_Vulkan()
    {
        Release();
    }

    void Release();

    virtual void Initialize() override;
    virtual void* GetHandle() const override { return vkPipeline; }
    virtual void* GetPipelineLayoutHandle() const override { return vkPipelineLayout; }
    virtual void* CreateGraphicsPipelineState() override;
    virtual void* CreateComputePipelineState() override;
    virtual void* CreateRaytracingPipelineState() override;
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;

    VkPipeline vkPipeline = nullptr;
    VkPipelineLayout vkPipelineLayout = nullptr;        // PipelineLayout 은 PipelineLayoutPool 에서 캐싱해둔 거라 소멸시키지 않음

    // Raytracing ShaderTables
    std::shared_ptr<jBuffer_Vulkan> RaygenBuffer;
    std::shared_ptr<jBuffer_Vulkan> MissBuffer;
    std::shared_ptr<jBuffer_Vulkan> HitGroupBuffer;

    VkStridedDeviceAddressRegionKHR RaygenStridedDeviceAddressRegion{};
    VkStridedDeviceAddressRegionKHR MissStridedDeviceAddressRegion{};
    VkStridedDeviceAddressRegionKHR HitStridedDeviceAddressRegion{};
};
