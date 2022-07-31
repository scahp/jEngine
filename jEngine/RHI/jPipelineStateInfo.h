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
        Hash ^= CityHash64((const char*)&Y, sizeof(Y));
        Hash ^= CityHash64((const char*)&Width, sizeof(Width));
        Hash ^= CityHash64((const char*)&Height, sizeof(Height));
        Hash ^= CityHash64((const char*)&MinDepth, sizeof(MinDepth));
        Hash ^= CityHash64((const char*)&MaxDepth, sizeof(MaxDepth));
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
        Hash ^= CityHash64((const char*)&Extent, sizeof(Extent));
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
        result ^= CityHash64((const char*)&Magnification, sizeof(Magnification));
        result ^= CityHash64((const char*)&AddressU, sizeof(AddressU));
        result ^= CityHash64((const char*)&AddressV, sizeof(AddressV));
        result ^= CityHash64((const char*)&AddressW, sizeof(AddressW));
        result ^= CityHash64((const char*)&MipLODBias, sizeof(MipLODBias));
        result ^= CityHash64((const char*)&MaxAnisotropy, sizeof(MaxAnisotropy));
        result ^= CityHash64((const char*)&BorderColor, sizeof(BorderColor));
        result ^= CityHash64((const char*)&MinLOD, sizeof(MinLOD));
        result ^= CityHash64((const char*)&MaxLOD, sizeof(MaxLOD));
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
        Hash ^= CityHash64((const char*)&CullMode, sizeof(CullMode));
        Hash ^= CityHash64((const char*)&FrontFace, sizeof(FrontFace));
        Hash ^= CityHash64((const char*)&DepthBiasEnable, sizeof(DepthBiasEnable));
        Hash ^= CityHash64((const char*)&DepthBiasConstantFactor, sizeof(DepthBiasConstantFactor));
        Hash ^= CityHash64((const char*)&DepthBiasClamp, sizeof(DepthBiasClamp));
        Hash ^= CityHash64((const char*)&DepthBiasSlopeFactor, sizeof(DepthBiasSlopeFactor));
        Hash ^= CityHash64((const char*)&LineWidth, sizeof(LineWidth));
        Hash ^= CityHash64((const char*)&DepthClampEnable, sizeof(DepthClampEnable));
        Hash ^= CityHash64((const char*)&RasterizerDiscardEnable, sizeof(RasterizerDiscardEnable));
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
        Hash ^= CityHash64((const char*)&SampleShadingEnable, sizeof(SampleShadingEnable));
        Hash ^= CityHash64((const char*)&MinSampleShading, sizeof(MinSampleShading));
        Hash ^= CityHash64((const char*)&AlphaToCoverageEnable, sizeof(AlphaToCoverageEnable));
        Hash ^= CityHash64((const char*)&AlphaToOneEnable, sizeof(AlphaToOneEnable));
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
        Hash ^= CityHash64((const char*)&PassOp, sizeof(PassOp));
        Hash ^= CityHash64((const char*)&DepthFailOp, sizeof(DepthFailOp));
        Hash ^= CityHash64((const char*)&CompareOp, sizeof(CompareOp));
        Hash ^= CityHash64((const char*)&CompareMask, sizeof(CompareMask));
        Hash ^= CityHash64((const char*)&WriteMask, sizeof(WriteMask));
        Hash ^= CityHash64((const char*)&Reference, sizeof(Reference));
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
        Hash ^= CityHash64((const char*)&DepthWriteEnable, sizeof(DepthWriteEnable));
        Hash ^= CityHash64((const char*)&DepthCompareOp, sizeof(DepthCompareOp));
        Hash ^= CityHash64((const char*)&DepthBoundsTestEnable, sizeof(DepthBoundsTestEnable));
        Hash ^= CityHash64((const char*)&StencilTestEnable, sizeof(StencilTestEnable));
        if (Front)
            Hash ^= Front->GetHash();
        if (Back)
            Hash ^= Back->GetHash();
        Hash ^= CityHash64((const char*)&MinDepthBounds, sizeof(MinDepthBounds));
        Hash ^= CityHash64((const char*)&MaxDepthBounds, sizeof(MaxDepthBounds));
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
        Hash ^= CityHash64((const char*)&Src, sizeof(Src));
        Hash ^= CityHash64((const char*)&Dest, sizeof(Dest));
        Hash ^= CityHash64((const char*)&BlendOp, sizeof(BlendOp));
        Hash ^= CityHash64((const char*)&SrcAlpha, sizeof(SrcAlpha));
        Hash ^= CityHash64((const char*)&DestAlpha, sizeof(DestAlpha));
        Hash ^= CityHash64((const char*)&AlphaBlendOp, sizeof(AlphaBlendOp));
        Hash ^= CityHash64((const char*)&ColorWriteMask, sizeof(ColorWriteMask));
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
    //uint32_t                                      attachmentCount;
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

struct jShader;
struct jVertexBuffer;
struct jShaderBindings;
class jRenderPass;

//////////////////////////////////////////////////////////////////////////
// jPipelineStateInfo
//////////////////////////////////////////////////////////////////////////
struct jPipelineStateInfo
{
    jPipelineStateInfo() = default;
    jPipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader, const jVertexBuffer* vertexBuffer
        , const jRenderPass* renderPass, const std::vector<const jShaderBindings*> shaderBindings)
        : PipelineStateFixed(pipelineStateFixed), Shader(shader), VertexBuffer(vertexBuffer), RenderPass(renderPass), ShaderBindings(shaderBindings)
    {}
    jPipelineStateInfo(const jPipelineStateInfo& pipelineState)
        : PipelineStateFixed(pipelineState.PipelineStateFixed), Shader(pipelineState.Shader)
        , VertexBuffer(pipelineState.VertexBuffer), RenderPass(pipelineState.RenderPass), ShaderBindings(pipelineState.ShaderBindings)
    {}
    virtual ~jPipelineStateInfo() {}

    size_t GetHash() const;

    mutable size_t Hash = 0;

    const jShader* Shader = nullptr;
    const jVertexBuffer* VertexBuffer = nullptr;
    const jRenderPass* RenderPass = nullptr;
    std::vector<const jShaderBindings*> ShaderBindings;
    const jPipelineStateFixedInfo* PipelineStateFixed = nullptr;

    virtual void Initialize() {}
    virtual void* GetHandle() const { return nullptr; }
    virtual void* GetPipelineLayoutHandle() const { return nullptr; }
    virtual void* CreateGraphicsPipelineState() { return nullptr; }
    virtual void Bind() const { }
};
