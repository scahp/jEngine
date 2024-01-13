#pragma once
#include "jShaderBindingLayout.h"
#include "jBuffer.h"
#include "Shader/jShader.h"

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

        Hash = CityHash64((uint64)X);
        Hash = CityHash64WithSeed((uint64)Y, Hash);
        Hash = CityHash64WithSeed((uint64)Width, Hash);
        Hash = CityHash64WithSeed((uint64)Height, Hash);
        Hash = CityHash64WithSeed((uint64)MinDepth, Hash);
        Hash = CityHash64WithSeed((uint64)MaxDepth, Hash);
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

        Hash = CityHash64((uint64)Offset.x);
        Hash = CityHash64WithSeed((uint64)Offset.y, Hash);
        Hash = CityHash64WithSeed((uint64)Extent.x, Hash);
        Hash = CityHash64WithSeed((uint64)Extent.y, Hash);
        return Hash;
    }
};

//////////////////////////////////////////////////////////////////////////
// jSamplerStateInfo
//////////////////////////////////////////////////////////////////////////
struct jSamplerStateInfo : public jShaderBindableResource
{
    virtual ~jSamplerStateInfo() {}
    virtual void Initialize() { GetHash(); }
    virtual void* GetHandle() const { return nullptr; }

    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((uint64)Minification);
        Hash = CityHash64WithSeed((uint64)Magnification, Hash);
        Hash = CityHash64WithSeed((uint64)AddressU, Hash);
        Hash = CityHash64WithSeed((uint64)AddressV, Hash);
        Hash = CityHash64WithSeed((uint64)AddressW, Hash);
        Hash = CityHash64WithSeed((uint64)MipLODBias, Hash);
        Hash = CityHash64WithSeed((uint64)MaxAnisotropy, Hash);
        Hash = CityHash64WithSeed((uint64)TextureComparisonMode, Hash);
        Hash = CityHash64WithSeed((uint64)IsEnableComparisonMode, Hash);
        Hash = CityHash64WithSeed((uint64)ComparisonFunc, Hash);
        Hash = CityHash64WithSeed((uint64)ComparisonFunc, Hash);
        Hash = CityHash64WithSeed((const char*)&BorderColor, sizeof(BorderColor), Hash);    // safe : BorderColor is 16 byte align data
        Hash = CityHash64WithSeed((uint64)MinLOD, Hash);
        Hash = CityHash64WithSeed((uint64)MaxLOD, Hash);
        return Hash;
    }

    std::string ToString() const
    {
        std::string Result;
        Result += EnumToString(Minification);
        Result += ",";
        Result += EnumToString(Magnification);
        Result += ",";
        Result += EnumToString(AddressU);
        Result += ",";
        Result += EnumToString(AddressV);
        Result += ",";
        Result += EnumToString(AddressW);
        Result += std::to_string(MipLODBias);
        Result += ",";
        Result += std::to_string(MaxAnisotropy);
        Result += ",";
        Result += EnumToString(TextureComparisonMode);
        Result += ",";
        Result += std::to_string(IsEnableComparisonMode);
        Result += ",";
        Result += EnumToString(ComparisonFunc);
        Result += ",";
        Result += std::to_string(MaxAnisotropy);
        Result += ",";

        Result += "(";
        Result += std::to_string(BorderColor.x);
        Result += ",";
        Result += std::to_string(BorderColor.y);
        Result += ",";
        Result += std::to_string(BorderColor.z);
        Result += ",";
        Result += std::to_string(BorderColor.w);
        Result += ")";
        Result += ",";

        Result += std::to_string(MinLOD);
        Result += ",";
        Result += std::to_string(MinLOD);
        Result += ",";

        return Result;
    }

    mutable size_t Hash = 0;

    ETextureFilter Minification = ETextureFilter::NEAREST;
    ETextureFilter Magnification = ETextureFilter::NEAREST;
    ETextureAddressMode AddressU = ETextureAddressMode::CLAMP_TO_EDGE;
    ETextureAddressMode AddressV = ETextureAddressMode::CLAMP_TO_EDGE;
    ETextureAddressMode AddressW = ETextureAddressMode::CLAMP_TO_EDGE;
    float MipLODBias = 0.0f;
    float MaxAnisotropy = 1.0f;			// if you anisotropy filtering tuned on, set this variable greater than 1.
    ETextureComparisonMode TextureComparisonMode = ETextureComparisonMode::NONE;
    bool IsEnableComparisonMode = false;
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
    virtual void Initialize() { GetHash(); }
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((uint64)PolygonMode);
        Hash = CityHash64WithSeed((uint64)CullMode, Hash);
        Hash = CityHash64WithSeed((uint64)FrontFace, Hash);
        Hash = CityHash64WithSeed((uint64)DepthBiasEnable, Hash);
        Hash = CityHash64WithSeed((uint64)DepthBiasConstantFactor, Hash);
        Hash = CityHash64WithSeed((uint64)DepthBiasClamp, Hash);
        Hash = CityHash64WithSeed((uint64)DepthBiasSlopeFactor, Hash);
        Hash = CityHash64WithSeed((uint64)LineWidth, Hash);
        Hash = CityHash64WithSeed((uint64)DepthClampEnable, Hash);
        Hash = CityHash64WithSeed((uint64)RasterizerDiscardEnable, Hash);
        Hash = CityHash64WithSeed((uint64)SampleCount, Hash);
        Hash = CityHash64WithSeed((uint64)SampleShadingEnable, Hash);
        Hash = CityHash64WithSeed((uint64)MinSampleShading, Hash);
        Hash = CityHash64WithSeed((uint64)AlphaToCoverageEnable, Hash);
        Hash = CityHash64WithSeed((uint64)AlphaToOneEnable, Hash);
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
    virtual void Initialize() { GetHash(); }
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((uint64)FailOp);
        Hash = CityHash64WithSeed((uint64)PassOp, Hash);
        Hash = CityHash64WithSeed((uint64)DepthFailOp, Hash);
        Hash = CityHash64WithSeed((uint64)CompareOp, Hash);
        Hash = CityHash64WithSeed((uint64)CompareMask, Hash);
        Hash = CityHash64WithSeed((uint64)WriteMask, Hash);
        Hash = CityHash64WithSeed((uint64)Reference, Hash);
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
    virtual void Initialize() { GetHash(); }
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((uint64)DepthTestEnable);
        Hash = CityHash64WithSeed((uint64)DepthWriteEnable, Hash);
        Hash = CityHash64WithSeed((uint64)DepthCompareOp, Hash);
        Hash = CityHash64WithSeed((uint64)DepthBoundsTestEnable, Hash);
        Hash = CityHash64WithSeed((uint64)StencilTestEnable, Hash);
        if (Front)
            Hash = CityHash64WithSeed(Front->GetHash(), Hash);
        if (Back)
            Hash = CityHash64WithSeed(Back->GetHash(), Hash);
        Hash = CityHash64WithSeed((uint64)MinDepthBounds, Hash);
        Hash = CityHash64WithSeed((uint64)MaxDepthBounds, Hash);
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
    virtual void Initialize() { GetHash(); }
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((uint64)BlendEnable);
        Hash = CityHash64WithSeed((uint64)Src, Hash);
        Hash = CityHash64WithSeed((uint64)Dest, Hash);
        Hash = CityHash64WithSeed((uint64)BlendOp, Hash);
        Hash = CityHash64WithSeed((uint64)SrcAlpha, Hash);
        Hash = CityHash64WithSeed((uint64)DestAlpha, Hash);
        Hash = CityHash64WithSeed((uint64)AlphaBlendOp, Hash);
        Hash = CityHash64WithSeed((uint64)ColorWriteMask, Hash);
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
    jPipelineStateFixedInfo(jRasterizationStateInfo* rasterizationState, jDepthStencilStateInfo* depthStencilState
        , jBlendingStateInfo* blendingState, const std::vector<jViewport>& viewports, const std::vector<jScissor>& scissors, bool isUseVRS)
        : RasterizationState(rasterizationState), DepthStencilState(depthStencilState)
        , BlendingState(blendingState), Viewports(Viewports), Scissors(scissors), IsUseVRS(isUseVRS)
    {
        CreateHash();
    }
    jPipelineStateFixedInfo(jRasterizationStateInfo* rasterizationState, jDepthStencilStateInfo* depthStencilState
        , jBlendingStateInfo* blendingState, const jViewport& viewport, const jScissor& scissor, bool isUseVRS)
        : RasterizationState(rasterizationState), DepthStencilState(depthStencilState)
        , BlendingState(blendingState), Viewports({ viewport }), Scissors({ scissor }), IsUseVRS(isUseVRS)
    {
        CreateHash();
    }
    jPipelineStateFixedInfo(jRasterizationStateInfo* rasterizationState, jDepthStencilStateInfo* depthStencilState
        , jBlendingStateInfo* blendingState, const std::vector<EPipelineDynamicState>& InDynamicStates, bool isUseVRS)
        : RasterizationState(rasterizationState), DepthStencilState(depthStencilState)
        , BlendingState(blendingState), DynamicStates(InDynamicStates), IsUseVRS(isUseVRS)
    {
        CreateHash();
    }

    size_t CreateHash() const
    {
        if (Hash)
            return Hash;

        Hash = 0;
        for (int32 i = 0; i < Viewports.size(); ++i)
            Hash ^= (Viewports[i].GetHash() ^ (i + 1));

        for (int32 i = 0; i < Scissors.size(); ++i)
            Hash ^= (Scissors[i].GetHash() ^ (i + 1));

        if (DynamicStates.size() > 0)
            Hash = CityHash64WithSeed((const char*)&DynamicStates[0], sizeof(EPipelineDynamicState) * DynamicStates.size(), Hash);

        // 아래 내용들도 해시를 만들 수 있어야 함, todo
        Hash ^= RasterizationState->GetHash();
        Hash ^= DepthStencilState->GetHash();
        Hash ^= BlendingState->GetHash();
        Hash ^= (uint64)IsUseVRS;

        return Hash;
    }

    std::vector<jViewport> Viewports;
    std::vector<jScissor> Scissors;
    std::vector<EPipelineDynamicState> DynamicStates;

    jRasterizationStateInfo* RasterizationState = nullptr;
    jDepthStencilStateInfo* DepthStencilState = nullptr;
    jBlendingStateInfo* BlendingState = nullptr;
    bool IsUseVRS = false;

    mutable size_t Hash = 0;
};

struct jPushConstantRange
{
    jPushConstantRange() = default;
    jPushConstantRange(EShaderAccessStageFlag accessStageFlag, int32 offset, int32 size)
        : AccessStageFlag(accessStageFlag), Offset(offset), Size(size)
    {}

    EShaderAccessStageFlag AccessStageFlag = EShaderAccessStageFlag::ALL_GRAPHICS;
    int32 Offset = 0;
    int32 Size = 0;
};

struct jPushConstant
{
    jPushConstant() = default;
    jPushConstant(const jPushConstant& InPushConstant)
    {
        check(InPushConstant.UsedSize < 256);

        UsedSize = InPushConstant.UsedSize;
        memcpy(Data, InPushConstant.Data, InPushConstant.UsedSize);
        PushConstantRanges = InPushConstant.PushConstantRanges;
        Hash = InPushConstant.Hash;
    }
    jPushConstant(const char* InData, int32 InSize, EShaderAccessStageFlag InShaderAccessStageFlag)
    {
        check(InSize < 256);

        UsedSize = InSize;
        memcpy(Data, InData, InSize);
        PushConstantRanges.Add(jPushConstantRange(InShaderAccessStageFlag, 0, InSize));
        GetHash();
    }
    jPushConstant(const char* InData, int32 InSize, const jPushConstantRange& InPushConstantRange)
    {
        check(InSize < 256);

        UsedSize = InSize;
        memcpy(Data, InData, InSize);
        PushConstantRanges.Add(InPushConstantRange);
        GetHash();
    }
    jPushConstant(const char* InData, int32 InSize, const jResourceContainer<jPushConstantRange>& InPushConstantRanges)
        : PushConstantRanges(InPushConstantRanges)
    {
        check(InSize < 256);

        UsedSize = InSize;
        memcpy(Data, InData, InSize);
        GetHash();
    }
    
    template <typename T>
    jPushConstant(const T& InData, EShaderAccessStageFlag InShaderAccessStageFlag)
    {
        Set(InData, jPushConstantRange(InShaderAccessStageFlag, 0, sizeof(T)));
    }

    template <typename T>
    jPushConstant(const T& InData, const jPushConstantRange& InPushConstantRange)
    {
        Set(InData, InPushConstantRange);
    }

    template <typename T>
    jPushConstant(const T& InData, const jResourceContainer<jPushConstantRange>& InPushConstantRanges)
    {
        Set(InData, InPushConstantRanges);
    }

    template <typename T>
    void Set(const T& InData, const jPushConstantRange& InPushConstantRange)
    {
        check(sizeof(T) < 256);

        UsedSize = sizeof(T);
        memcpy(Data, &InData, sizeof(T));
        PushConstantRanges.Add(InPushConstantRange);
        GetHash();
    }

    template <typename T>
    void Set(const T& InData, const jResourceContainer<jPushConstantRange>& InPushConstantRanges)
    {
        check(sizeof(T) < 256);

        UsedSize = sizeof(T);
        memcpy(Data, &InData, sizeof(T));
        PushConstantRanges = InPushConstantRanges;
        GetHash();
    }

    template <typename T>
    T& Get() const
    {
        return *(T*)&Data[0];
    }

    FORCEINLINE bool IsValid() const
    {
        return UsedSize > 0;
    }

    jPushConstant& operator = (const jPushConstant& InPushConstant)
    {
        check(InPushConstant.UsedSize < 256);

        UsedSize = InPushConstant.UsedSize;
        memcpy(Data, InPushConstant.Data, InPushConstant.UsedSize);
        PushConstantRanges = InPushConstant.PushConstantRanges;
        Hash = InPushConstant.Hash;
        return *this;
    }

    size_t GetHash() const
    {
        if (Hash)
            return Hash;

        if (PushConstantRanges.NumOfData > 0)
            CityHash64WithSeed((const char*)&PushConstantRanges[0], sizeof(jPushConstantRange) * PushConstantRanges.NumOfData, Hash);

        return Hash;
    }
    const void* GetConstantData() const { return (void*)&Data[0]; }
    int32 GetSize() const { return UsedSize; }
    const jResourceContainer<jPushConstantRange>* GetPushConstantRanges() const { return &PushConstantRanges; }

    mutable size_t Hash = 0;
    jResourceContainer<jPushConstantRange> PushConstantRanges;
    uint8 Data[256];
    int32 UsedSize = 0;
};

struct jShader;
struct jGraphicsPipelineShader;
struct jVertexBuffer;
struct jShaderBindingLayout;
class jRenderPass;
struct jRenderFrameContext;

//////////////////////////////////////////////////////////////////////////
// jPipelineStateInfo
//////////////////////////////////////////////////////////////////////////
struct jPipelineStateInfo
{
    enum class EPipelineType : uint8
    {
        Graphics = 0,
        Compute,
        RayTracing
    };

    jPipelineStateInfo() = default;
    jPipelineStateInfo(const jPipelineStateFixedInfo* InPipelineStateFixed, const jGraphicsPipelineShader InShader, const jVertexBufferArray& InVertexBufferArray
        , const jRenderPass* InRenderPass, const jShaderBindingLayoutArray& InShaderBindingLayoutArray, const jPushConstant* InPushConstant = nullptr, int32 InSubpassIndex = 0)
        : PipelineStateFixed(InPipelineStateFixed), GraphicsShader(InShader), VertexBufferArray(InVertexBufferArray), RenderPass(InRenderPass), ShaderBindingLayoutArray(InShaderBindingLayoutArray)
        , PushConstant(InPushConstant), SubpassIndex(InSubpassIndex)
    {
        PipelineType = EPipelineType::Graphics;
    }
    jPipelineStateInfo(const jShader* InComputeShader, const jShaderBindingLayoutArray& InShaderBindingLayoutArray, const jPushConstant* InPushConstant = nullptr, int32 InSubpassIndex = 0)
        : ComputeShader(InComputeShader), ShaderBindingLayoutArray(InShaderBindingLayoutArray), PushConstant(InPushConstant), SubpassIndex(InSubpassIndex)
    {
        PipelineType = EPipelineType::Compute;
    }
    jPipelineStateInfo(const std::vector<jRaytracingPipelineShader>& InShader, const jRaytracingPipelineData& InRaytracingPipelineData
        , const jShaderBindingLayoutArray& InShaderBindingLayoutArray, const jPushConstant* InPushConstant = nullptr, int32 InSubpassIndex = 0)
        : RaytracingShaders(InShader), RaytracingPipelineData(InRaytracingPipelineData), ShaderBindingLayoutArray(InShaderBindingLayoutArray), PushConstant(InPushConstant), SubpassIndex(InSubpassIndex)
    {
        PipelineType = EPipelineType::RayTracing;
    }
    jPipelineStateInfo(const jPipelineStateInfo& InPipelineState)
        : PipelineStateFixed(InPipelineState.PipelineStateFixed), GraphicsShader(InPipelineState.GraphicsShader), ComputeShader(InPipelineState.ComputeShader), RaytracingShaders(InPipelineState.RaytracingShaders)
        , PipelineType(InPipelineState.PipelineType), VertexBufferArray(InPipelineState.VertexBufferArray), RenderPass(InPipelineState.RenderPass), ShaderBindingLayoutArray(InPipelineState.ShaderBindingLayoutArray)
        , PushConstant(InPipelineState.PushConstant), Hash(InPipelineState.Hash), SubpassIndex(InPipelineState.SubpassIndex), RaytracingPipelineData(InPipelineState.RaytracingPipelineData)
    {}
    jPipelineStateInfo(jPipelineStateInfo&& InPipelineState) noexcept
        : PipelineStateFixed(InPipelineState.PipelineStateFixed), GraphicsShader(InPipelineState.GraphicsShader), ComputeShader(InPipelineState.ComputeShader), RaytracingShaders(InPipelineState.RaytracingShaders)
        , PipelineType(InPipelineState.PipelineType), VertexBufferArray(InPipelineState.VertexBufferArray), RenderPass(InPipelineState.RenderPass), ShaderBindingLayoutArray(InPipelineState.ShaderBindingLayoutArray)
        , PushConstant(InPipelineState.PushConstant), Hash(InPipelineState.Hash), SubpassIndex(InPipelineState.SubpassIndex), RaytracingPipelineData(InPipelineState.RaytracingPipelineData)
    {}
    virtual ~jPipelineStateInfo() {}

    size_t GetHash() const;

    mutable size_t Hash = 0;

    EPipelineType PipelineType = EPipelineType::Graphics;
    const jGraphicsPipelineShader GraphicsShader;
    const jShader* ComputeShader = nullptr;
    std::vector<jRaytracingPipelineShader> RaytracingShaders;
    jRaytracingPipelineData RaytracingPipelineData;
    const jRenderPass* RenderPass = nullptr;
    jVertexBufferArray VertexBufferArray;
    jShaderBindingLayoutArray ShaderBindingLayoutArray;
    const jPushConstant* PushConstant;
    const jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
    int32 SubpassIndex = 0;

    virtual void Initialize() { GetHash(); }
    virtual void* GetHandle() const { return nullptr; }
    virtual void* GetPipelineLayoutHandle() const { return nullptr; }
    virtual void* CreateGraphicsPipelineState() { return nullptr; }
    virtual void* CreateComputePipelineState() { return nullptr; }
    virtual void* CreateRaytracingPipelineState() { return nullptr; }
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const { }
};
