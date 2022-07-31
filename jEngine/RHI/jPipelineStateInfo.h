#pragma once

struct jPipelineStateFixedInfo
{
    jPipelineStateFixedInfo() = default;
    jPipelineStateFixedInfo(jRasterizationStateInfo* rasterizationState, jMultisampleStateInfo* multisampleState, jDepthStencilStateInfo* depthStencilState
        , jBlendingStateInfo* blendingState, const std::vector<jViewport>& viewports, const std::vector<jScissor>& scissors)
        : RasterizationState(rasterizationState), MultisampleState(multisampleState), DepthStencilState(depthStencilState)
        , BlendingState(blendingState), Viewports(Viewports), Scissors(scissors)
    {}
    jPipelineStateFixedInfo(jRasterizationStateInfo* rasterizationState, jMultisampleStateInfo* multisampleState, jDepthStencilStateInfo* depthStencilState
        , jBlendingStateInfo* blendingState, const jViewport& viewport, const jScissor& scissor)
        : RasterizationState(rasterizationState), MultisampleState(multisampleState), DepthStencilState(depthStencilState)
        , BlendingState(blendingState), Viewports({ viewport }), Scissors({ scissor })
    {}

    size_t CreateHash() const
    {
        if (Hash)
            return Hash;

        Hash = 0;
        for (int32 i = 0; i < Viewports.size(); ++i)
            Hash ^= Viewports[i].GetHash();

        for (int32 i = 0; i < Scissors.size(); ++i)
            Hash ^= Scissors[i].GetHash();

        // 아래 내용들도 해시를 만들 수 있어야 함, todo
        Hash ^= RasterizationState->GetHash();
        Hash ^= MultisampleState->GetHash();
        Hash ^= DepthStencilState->GetHash();
        Hash ^= BlendingState->GetHash();

        return Hash;
    }

    std::vector<jViewport> Viewports;
    std::vector<jScissor> Scissors;

    jRasterizationStateInfo* RasterizationState = nullptr;
    jMultisampleStateInfo* MultisampleState = nullptr;
    jDepthStencilStateInfo* DepthStencilState = nullptr;
    jBlendingStateInfo* BlendingState = nullptr;

    mutable size_t Hash = 0;
};

struct jPipelineStateInfo
{
    jPipelineStateInfo() = default;
    jPipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader, const jVertexBuffer* vertexBuffer, const jRenderPass* renderPass, const std::vector<const jShaderBindings*> shaderBindings)
        : PipelineStateFixed(pipelineStateFixed), Shader(shader), VertexBuffer(vertexBuffer), RenderPass(renderPass), ShaderBindings(shaderBindings)
    {}

    FORCEINLINE size_t GetHash() const
    {
        if (Hash)
            return Hash;

        check(PipelineStateFixed);
        Hash = PipelineStateFixed->CreateHash();

        Hash ^= Shader->ShaderInfo.GetHash();
        Hash ^= VertexBuffer->GetHash();
        Hash ^= jShaderBindings::CreateShaderBindingsHash(ShaderBindings);
        Hash ^= RenderPass->GetHash();

        return Hash;
    }

    mutable size_t Hash = 0;

    const jShader* Shader = nullptr;
    const jVertexBuffer* VertexBuffer = nullptr;
    const jRenderPass* RenderPass = nullptr;
    std::vector<const jShaderBindings*> ShaderBindings;
    const jPipelineStateFixedInfo* PipelineStateFixed = nullptr;

    VkPipeline vkPipeline = nullptr;
    VkPipelineLayout vkPipelineLayout = nullptr;

    VkPipeline CreateGraphicsPipelineState();
    void Bind();

    static std::unordered_map<size_t, jPipelineStateInfo> PipelineStatePool;
};