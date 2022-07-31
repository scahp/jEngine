#pragma once

struct jSamplerStateInfo_Vulkan : public jSamplerStateInfo
{
    jSamplerStateInfo_Vulkan() = default;
    jSamplerStateInfo_Vulkan(const jSamplerStateInfo& state) : jSamplerStateInfo(state) {}
    virtual ~jSamplerStateInfo_Vulkan() {}
    virtual void Initialize() override;

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
};

struct jMultisampleStateInfo_Vulkan : public jMultisampleStateInfo
{
    jMultisampleStateInfo_Vulkan() = default;
    jMultisampleStateInfo_Vulkan(const jMultisampleStateInfo& state) : jMultisampleStateInfo(state) {}
    virtual ~jMultisampleStateInfo_Vulkan() {}
    virtual void Initialize() override;

    VkPipelineMultisampleStateCreateInfo MultisampleStateInfo = {};
};

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

struct jBlendingStateInfo_Vulakn : public jBlendingStateInfo
{
    jBlendingStateInfo_Vulakn() = default;
    jBlendingStateInfo_Vulakn(const jBlendingStateInfo& state) : jBlendingStateInfo(state) {}
    virtual ~jBlendingStateInfo_Vulakn() {}
    virtual void Initialize() override;

    VkPipelineColorBlendAttachmentState ColorBlendAttachmentInfo = {};
};

//////////////////////////////////////////////////////////////////////////
// jPipelineStateInfo_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jPipelineStateInfo_Vulkan : public jPipelineStateInfo
{
    jPipelineStateInfo_Vulkan() = default;
    jPipelineStateInfo_Vulkan(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader
        , const jVertexBuffer* vertexBuffer, const jRenderPass* renderPass, const std::vector<const jShaderBindings*> shaderBindings)
        : jPipelineStateInfo(pipelineStateFixed, shader, vertexBuffer, renderPass, shaderBindings)
    {}
    jPipelineStateInfo_Vulkan(const jPipelineStateInfo& pipelineState)
        : jPipelineStateInfo(pipelineState)
    {}
    virtual ~jPipelineStateInfo_Vulkan() {}

    virtual void Initialize() override;
    virtual void* GetHandle() const override { return vkPipeline; }
    virtual void* GetPipelineLayoutHandle() const override { return vkPipelineLayout; }
    virtual void* CreateGraphicsPipelineState() override;
    virtual void Bind() const override;

    VkPipeline vkPipeline = nullptr;
    VkPipelineLayout vkPipelineLayout = nullptr;
};
