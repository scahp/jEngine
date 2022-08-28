#pragma once

//////////////////////////////////////////////////////////////////////////
// jViewport
//////////////////////////////////////////////////////////////////////////
struct jViewport
{
    jViewport() = default;
    jViewport(int32 x, int32 y, int32 width, int32 height, float minDepth = 0.0f, float maxDepth = 1.0f)
        : X(static_cast<float>(x)), Y(static_cast<float>(y))
        , Width(static_cast<float>(width)), Height(static_cast<float>(height))
        , MinDepth(minDepth), MaxDepth(maxDepth)
    {}
    jViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f) : X(x), Y(y), Width(width), Height(height), MinDepth(minDepth), MaxDepth(maxDepth) {}

    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
    float MinDepth = 0.0f;
    float MaxDepth = 1.0f;

    mutable size_t Hash = 0;

    size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((const char*)&X, sizeof(X));
        Hash = CityHash64WithSeed((const char*)&Y, sizeof(Y), Hash);
        Hash = CityHash64WithSeed((const char*)&Width, sizeof(Width), Hash);
        Hash = CityHash64WithSeed((const char*)&Height, sizeof(Height), Hash);
        Hash = CityHash64WithSeed((const char*)&MinDepth, sizeof(MinDepth), Hash);
        Hash = CityHash64WithSeed((const char*)&MaxDepth, sizeof(MaxDepth), Hash);
        return Hash;
    }
};

//////////////////////////////////////////////////////////////////////////
// jScissor
//////////////////////////////////////////////////////////////////////////
struct jScissor
{
    jScissor() = default;
    jScissor(int32 x, int32 y, int32 width, int32 height)
        : Offset(x, y), Extent(width, height)
    {}
    Vector2i Offset;
    Vector2i Extent;

    mutable size_t Hash = 0;

    FORCEINLINE size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((const char*)&Offset, sizeof(Offset));
        Hash = CityHash64WithSeed((const char*)&Extent, sizeof(Extent), Hash);
        return Hash;
    }
};

//////////////////////////////////////////////////////////////////////////
// jSamplerStateInfo
//////////////////////////////////////////////////////////////////////////
struct jSamplerStateInfo
{
    virtual ~jSamplerStateInfo() {}
    virtual void Initialize() {}
    virtual void* GetHandle() const { return nullptr; }

    virtual size_t GetHash() const
    {
        size_t result = CityHash64((const char*)&Minification, sizeof(Minification));
        result = CityHash64WithSeed((const char*)&Magnification, sizeof(Magnification), result);
        result = CityHash64WithSeed((const char*)&AddressU, sizeof(AddressU), result);
        result = CityHash64WithSeed((const char*)&AddressV, sizeof(AddressV), result);
        result = CityHash64WithSeed((const char*)&AddressW, sizeof(AddressW), result);
        result = CityHash64WithSeed((const char*)&MipLODBias, sizeof(MipLODBias), result);
        result = CityHash64WithSeed((const char*)&MaxAnisotropy, sizeof(MaxAnisotropy), result);
        result = CityHash64WithSeed((const char*)&BorderColor, sizeof(BorderColor), result);
        result = CityHash64WithSeed((const char*)&MinLOD, sizeof(MinLOD), result);
        result = CityHash64WithSeed((const char*)&MaxLOD, sizeof(MaxLOD), result);
        return result;
    }

    ETextureFilter Minification = ETextureFilter::NEAREST;
    ETextureFilter Magnification = ETextureFilter::NEAREST;
    ETextureAddressMode AddressU = ETextureAddressMode::CLAMP_TO_EDGE;
    ETextureAddressMode AddressV = ETextureAddressMode::CLAMP_TO_EDGE;
    ETextureAddressMode AddressW = ETextureAddressMode::CLAMP_TO_EDGE;
    float MipLODBias = 0.0f;
    float MaxAnisotropy = 1.0f;			// if you anisotropy filtering tuned on, set this variable greater than 1.
    ETextureComparisonMode TextureComparisonMode = ETextureComparisonMode::NONE;
    ECompareOp ComparisonFunc = ECompareOp::LESS;
    Vector4 BorderColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
    float MinLOD = -FLT_MAX;
    float MaxLOD = FLT_MAX;
};

//////////////////////////////////////////////////////////////////////////
// jRasterizationStateInfo
//////////////////////////////////////////////////////////////////////////
struct jRasterizationStateInfo
{
    virtual ~jRasterizationStateInfo() {}
    virtual void Initialize() {}
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((const char*)&PolygonMode, sizeof(PolygonMode));
        Hash = CityHash64WithSeed((const char*)&CullMode, sizeof(CullMode), Hash);
        Hash = CityHash64WithSeed((const char*)&FrontFace, sizeof(FrontFace), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthBiasEnable, sizeof(DepthBiasEnable), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthBiasConstantFactor, sizeof(DepthBiasConstantFactor), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthBiasClamp, sizeof(DepthBiasClamp), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthBiasSlopeFactor, sizeof(DepthBiasSlopeFactor), Hash);
        Hash = CityHash64WithSeed((const char*)&LineWidth, sizeof(LineWidth), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthClampEnable, sizeof(DepthClampEnable), Hash);
        Hash = CityHash64WithSeed((const char*)&RasterizerDiscardEnable, sizeof(RasterizerDiscardEnable), Hash);
        return Hash;
    }

    mutable size_t Hash = 0;

    EPolygonMode PolygonMode = EPolygonMode::FILL;
    ECullMode CullMode = ECullMode::BACK;
    EFrontFace FrontFace = EFrontFace::CCW;
    bool DepthBiasEnable = false;
    float DepthBiasConstantFactor = 0.0f;
    float DepthBiasClamp = 0.0f;
    float DepthBiasSlopeFactor = 0.0f;
    float LineWidth = 1.0f;
    bool DepthClampEnable = false;
    bool RasterizerDiscardEnable = false;
};

//////////////////////////////////////////////////////////////////////////
// jMultisampleStateInfo
//////////////////////////////////////////////////////////////////////////
struct jMultisampleStateInfo
{
    virtual ~jMultisampleStateInfo() {}
    virtual void Initialize() {}
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((const char*)&SampleCount, sizeof(SampleCount));
        Hash = CityHash64WithSeed((const char*)&SampleShadingEnable, sizeof(SampleShadingEnable), Hash);
        Hash = CityHash64WithSeed((const char*)&MinSampleShading, sizeof(MinSampleShading), Hash);
        Hash = CityHash64WithSeed((const char*)&AlphaToCoverageEnable, sizeof(AlphaToCoverageEnable), Hash);
        Hash = CityHash64WithSeed((const char*)&AlphaToOneEnable, sizeof(AlphaToOneEnable), Hash);
        return Hash;
    }

    mutable size_t Hash = 0;

    EMSAASamples SampleCount = EMSAASamples::COUNT_1;
    bool SampleShadingEnable = true;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
    float MinSampleShading = 0.2f;
    bool AlphaToCoverageEnable = false;
    bool AlphaToOneEnable = false;
};

//////////////////////////////////////////////////////////////////////////
// jStencilOpStateInfo
//////////////////////////////////////////////////////////////////////////
struct jStencilOpStateInfo
{
    virtual ~jStencilOpStateInfo() {}
    virtual void Initialize() {}
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((const char*)&FailOp, sizeof(FailOp));
        Hash = CityHash64WithSeed((const char*)&PassOp, sizeof(PassOp), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthFailOp, sizeof(DepthFailOp), Hash);
        Hash = CityHash64WithSeed((const char*)&CompareOp, sizeof(CompareOp), Hash);
        Hash = CityHash64WithSeed((const char*)&CompareMask, sizeof(CompareMask), Hash);
        Hash = CityHash64WithSeed((const char*)&WriteMask, sizeof(WriteMask), Hash);
        Hash = CityHash64WithSeed((const char*)&Reference, sizeof(Reference), Hash);
        return Hash;
    }

    mutable size_t Hash = 0;

    EStencilOp FailOp = EStencilOp::KEEP;
    EStencilOp PassOp = EStencilOp::KEEP;
    EStencilOp DepthFailOp = EStencilOp::KEEP;
    ECompareOp CompareOp = ECompareOp::NEVER;
    uint32 CompareMask = 0;
    uint32 WriteMask = 0;
    uint32 Reference = 0;
};

//////////////////////////////////////////////////////////////////////////
// jDepthStencilStateInfo
//////////////////////////////////////////////////////////////////////////
struct jDepthStencilStateInfo
{
    virtual ~jDepthStencilStateInfo() {}
    virtual void Initialize() {}
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((const char*)&DepthTestEnable, sizeof(DepthTestEnable));
        Hash = CityHash64WithSeed((const char*)&DepthWriteEnable, sizeof(DepthWriteEnable), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthCompareOp, sizeof(DepthCompareOp), Hash);
        Hash = CityHash64WithSeed((const char*)&DepthBoundsTestEnable, sizeof(DepthBoundsTestEnable), Hash);
        Hash = CityHash64WithSeed((const char*)&StencilTestEnable, sizeof(StencilTestEnable), Hash);
        if (Front)
            Hash = CityHash64WithSeed(Front->GetHash(), Hash);
        if (Back)
            Hash = CityHash64WithSeed(Back->GetHash(), Hash);
        Hash = CityHash64WithSeed((const char*)&MinDepthBounds, sizeof(MinDepthBounds), Hash);
        Hash = CityHash64WithSeed((const char*)&MaxDepthBounds, sizeof(MaxDepthBounds), Hash);
        return Hash;
    }

    mutable size_t Hash = 0;

    bool DepthTestEnable = false;
    bool DepthWriteEnable = false;
    ECompareOp DepthCompareOp = ECompareOp::LEQUAL;
    bool DepthBoundsTestEnable = false;
    bool StencilTestEnable = false;
    jStencilOpStateInfo* Front = nullptr;
    jStencilOpStateInfo* Back = nullptr;
    float MinDepthBounds = 0.0f;
    float MaxDepthBounds = 1.0f;

    // VkPipelineDepthStencilStateCreateFlags    flags;
};

//////////////////////////////////////////////////////////////////////////
// jBlendingStateInfo
//////////////////////////////////////////////////////////////////////////
struct jBlendingStateInfo
{
    virtual ~jBlendingStateInfo() {}
    virtual void Initialize() {}
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((const char*)&BlendEnable, sizeof(BlendEnable));
        Hash = CityHash64WithSeed((const char*)&Src, sizeof(Src), Hash);
        Hash = CityHash64WithSeed((const char*)&Dest, sizeof(Dest), Hash);
        Hash = CityHash64WithSeed((const char*)&BlendOp, sizeof(BlendOp), Hash);
        Hash = CityHash64WithSeed((const char*)&SrcAlpha, sizeof(SrcAlpha), Hash);
        Hash = CityHash64WithSeed((const char*)&DestAlpha, sizeof(DestAlpha), Hash);
        Hash = CityHash64WithSeed((const char*)&AlphaBlendOp, sizeof(AlphaBlendOp), Hash);
        Hash = CityHash64WithSeed((const char*)&ColorWriteMask, sizeof(ColorWriteMask), Hash);
        return Hash;
    }

    mutable size_t Hash = 0;

    bool BlendEnable = false;
    EBlendFactor Src = EBlendFactor::SRC_COLOR;
    EBlendFactor Dest = EBlendFactor::ONE_MINUS_SRC_ALPHA;
    EBlendOp BlendOp = EBlendOp::ADD;
    EBlendFactor SrcAlpha = EBlendFactor::SRC_ALPHA;
    EBlendFactor DestAlpha = EBlendFactor::ONE_MINUS_SRC_ALPHA;
    EBlendOp AlphaBlendOp = EBlendOp::ADD;
    EColorMask ColorWriteMask = EColorMask::NONE;

    //VkPipelineColorBlendStateCreateFlags          flags;
    //VkBool32                                      logicOpEnable;
    //VkLogicOp                                     logicOp;
    //uint32                                      attachmentCount;
    //const VkPipelineColorBlendAttachmentState* pAttachments;
    //float                                         blendConstants[4];
};

//////////////////////////////////////////////////////////////////////////
// jPipelineStateFixedInfo
//////////////////////////////////////////////////////////////////////////
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
            Hash = CityHash64WithSeed(Viewports[i].GetHash(), Hash);

        for (int32 i = 0; i < Scissors.size(); ++i)
            Hash = CityHash64WithSeed(Scissors[i].GetHash(), Hash);

        // 아래 내용들도 해시를 만들 수 있어야 함, todo
        Hash = CityHash64WithSeed(RasterizationState->GetHash(), Hash);
        Hash = CityHash64WithSeed(MultisampleState->GetHash(), Hash);
        Hash = CityHash64WithSeed(DepthStencilState->GetHash(), Hash);
        Hash = CityHash64WithSeed(BlendingState->GetHash(), Hash);

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

struct jShader;
struct jVertexBuffer;
struct jShaderBindingsLayout;
class jRenderPass;
struct jRenderFrameContext;

//////////////////////////////////////////////////////////////////////////
// jPipelineStateInfo
//////////////////////////////////////////////////////////////////////////
struct jPipelineStateInfo
{
    jPipelineStateInfo() = default;
    jPipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader, std::vector<const jVertexBuffer*> vertexBuffers
        , const jRenderPass* renderPass, const std::vector<const jShaderBindingsLayout*> shaderBindings)
        : PipelineStateFixed(pipelineStateFixed), Shader(shader), VertexBuffers(vertexBuffers), RenderPass(renderPass), ShaderBindings(shaderBindings)
    {
        IsGraphics = true;
    }
    jPipelineStateInfo(const jShader* shader, const std::vector<const jShaderBindingsLayout*> shaderBindings)
        : Shader(shader), ShaderBindings(shaderBindings)
    {
        IsGraphics = false;
    }
    jPipelineStateInfo(const jPipelineStateInfo& pipelineState)
        : PipelineStateFixed(pipelineState.PipelineStateFixed), Shader(pipelineState.Shader), IsGraphics(pipelineState.IsGraphics)
        , VertexBuffers(pipelineState.VertexBuffers), RenderPass(pipelineState.RenderPass), ShaderBindings(pipelineState.ShaderBindings)
    {}
    virtual ~jPipelineStateInfo() {}

    size_t GetHash() const;

    mutable size_t Hash = 0;

    bool IsGraphics = true;
    const jShader* Shader = nullptr;
    std::vector<const jVertexBuffer*> VertexBuffers;
    const jRenderPass* RenderPass = nullptr;
    std::vector<const jShaderBindingsLayout*> ShaderBindings;
    const jPipelineStateFixedInfo* PipelineStateFixed = nullptr;

    virtual void Initialize() {}
    virtual void* GetHandle() const { return nullptr; }
    virtual void* GetPipelineLayoutHandle() const { return nullptr; }
    virtual void* CreateGraphicsPipelineState() { return nullptr; }
    virtual void* CreateComputePipelineState() { return nullptr; }
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const { }
};
