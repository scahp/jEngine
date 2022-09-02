#pragma once

struct jAttachment
{
    jAttachment() = default;
    jAttachment(const std::shared_ptr<jRenderTarget>& InRTPtr
        , EAttachmentLoadStoreOp InLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
        , EAttachmentLoadStoreOp InStencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
        , Vector4 InClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f)
        , Vector2 InClearDepth = Vector2(1.0f, 0.0f)
        , EImageLayout InInitialLayout = EImageLayout::UNDEFINED
        , EImageLayout InFinalLayout = EImageLayout::SHADER_READ_ONLY
    )
        : RenderTargetPtr(InRTPtr), LoadStoreOp(InLoadStoreOp), StencilLoadStoreOp(InStencilLoadStoreOp)
        , ClearColor(InClearColor), ClearDepth(InClearDepth), InitialLayout(InInitialLayout), FinalLayout(InFinalLayout)
    {}

    std::shared_ptr<jRenderTarget> RenderTargetPtr;

    // 아래 2가지 옵션은 렌더링 전, 후에 attachment에 있는 데이터에 무엇을 할지 결정하는 부분.
    // 1). loadOp
    //		- VK_ATTACHMENT_LOAD_OP_LOAD : attachment에 있는 내용을 그대로 유지
    //		- VK_ATTACHMENT_LOAD_OP_CLEAR : attachment에 있는 내용을 constant 모두 값으로 설정함.
    //		- VK_ATTACHMENT_LOAD_OP_DONT_CARE : attachment에 있는 내용에 대해 어떠한 것도 하지 않음. 정의되지 않은 상태.
    // 2). storeOp
    //		- VK_ATTACHMENT_STORE_OP_STORE : 그려진 내용이 메모리에 저장되고 추후에 읽어질 수 있음.
    //		- VK_ATTACHMENT_STORE_OP_DONT_CARE : 렌더링을 수행한 후에 framebuffer의 내용이 어떻게 될지 모름(정의되지 않음).
    EAttachmentLoadStoreOp LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE;
    EAttachmentLoadStoreOp StencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE;

    Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
    Vector2 ClearDepth = Vector2(1.0f, 0.0f);

    EImageLayout InitialLayout = EImageLayout::UNDEFINED;
    EImageLayout FinalLayout = EImageLayout::SHADER_READ_ONLY;

    FORCEINLINE bool IsValid() const
    {
        if (RenderTargetPtr)
            return true;
        return false;
    }

    FORCEINLINE size_t GetHash() const
    {
        if (Hash)
            return Hash;

        if (!RenderTargetPtr)
            return 0;

        Hash = RenderTargetPtr->GetHash();
        Hash = CityHash64WithSeed((const char*)&LoadStoreOp, sizeof(LoadStoreOp), Hash);
        Hash = CityHash64WithSeed((const char*)&StencilLoadStoreOp, sizeof(StencilLoadStoreOp), Hash);
        Hash = CityHash64WithSeed((const char*)&ClearColor, sizeof(ClearColor), Hash);
        Hash = CityHash64WithSeed((const char*)&ClearDepth, sizeof(ClearDepth), Hash);
        Hash = CityHash64WithSeed((const char*)&InitialLayout, sizeof(InitialLayout), Hash);
        Hash = CityHash64WithSeed((const char*)&FinalLayout, sizeof(FinalLayout), Hash);
        return Hash;
    }

    FORCEINLINE bool IsDepthAttachment() const
    {
        check(RenderTargetPtr);
        return IsDepthFormat(RenderTargetPtr->Info.Format);
    }

    FORCEINLINE bool IsResolveAttachment() const
    {
        return false;
    }

    mutable size_t Hash = 0;
};

struct jSubpass
{
    std::vector<int32> InputAttachments;

    std::vector<int32> OutputColorAttachments;
    std::optional<int32> OutputDepthAttachment;
    std::optional<int32> OutputResolveAttachment;

    size_t GetHash() const
    {
        size_t Hash = 0;
        if (InputAttachments.size() > 0)
            Hash = CityHash64WithSeed((const char*)InputAttachments.data(), InputAttachments.size() * sizeof(int32), Hash);
        if (OutputColorAttachments.size() > 0)
            Hash = CityHash64WithSeed((const char*)OutputColorAttachments.data(), OutputColorAttachments.size() * sizeof(int32), Hash);
        if (OutputDepthAttachment)
            Hash = CityHash64WithSeed((const char*)&OutputDepthAttachment.value(), sizeof(int32), Hash);
        if (OutputResolveAttachment)
            Hash = CityHash64WithSeed((const char*)&OutputResolveAttachment.value(), sizeof(int32), Hash);
        return Hash;
    }
};

struct jRenderPassInfo
{
    std::vector<jAttachment> Attachments;
    std::vector<jSubpass> Subpasses;

    FORCEINLINE void Reset()
    {
        Attachments.clear();
        Subpasses.clear();
    }

    FORCEINLINE size_t GetHash() const
    {
        int64 Hash = 0;
        for(const auto& iter : Attachments)
        {
            Hash = CityHash64WithSeed(iter.GetHash(), Hash);
        }
        for (const auto& iter : Subpasses)
        {
            Hash = CityHash64WithSeed(iter.GetHash(), Hash);
        }
        return Hash;
    }

    FORCEINLINE bool Validate() const
    {
        for (const auto& iter : Subpasses)
        {
            for (const auto& inputIndex : iter.InputAttachments)
            {
                if (!ensure(Attachments.size() > inputIndex))
                    return false;
            }
            for (const auto& outputIndex : iter.OutputColorAttachments)
            {
                if (!ensure(Attachments.size() > outputIndex))
                    return false;
            }
            if (iter.OutputDepthAttachment)
            {
                if (!ensure(Attachments.size() > iter.OutputDepthAttachment.value()))
                    return false;
            }
            if (iter.OutputResolveAttachment)
            {
                if (!ensure(Attachments.size() > iter.OutputResolveAttachment.value()))
                    return false;
            }
        }
        return true;
    }
};

class jRenderPass
{
public:
    virtual ~jRenderPass() {}

    jRenderPass() = default;
    jRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent)
    {
        SetAttachemnt(colorAttachments);
        SetRenderArea(offset, extent);
    }

    jRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const Vector2i& offset, const Vector2i& extent)
    {
        SetAttachemnt(colorAttachments, depthAttachment);
        SetRenderArea(offset, extent);
    }

    jRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const jAttachment& colorResolveAttachment, const Vector2i& offset, const Vector2i& extent)
    {
        SetAttachemnt(colorAttachments, depthAttachment, colorResolveAttachment);
        SetRenderArea(offset, extent);
    }

    void SetAttachemnt(const std::vector<jAttachment>& colorAttachments)
    {
        // Add output color attachment
        const int32 startColorIndex = (int32)RenderPassInfo.Attachments.size();
        RenderPassInfo.Attachments.insert(RenderPassInfo.Attachments.end(), colorAttachments.begin(), colorAttachments.end());

        if (RenderPassInfo.Subpasses.empty())
            RenderPassInfo.Subpasses.resize(1);

        for (int32 i = 0; i < (int32)colorAttachments.size(); ++i)
            RenderPassInfo.Subpasses[0].OutputColorAttachments.push_back(startColorIndex + i);
    }

    void SetAttachemnt(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment)
    {
        // Add output color attachment
        const int32 startColorIndex = (int32)RenderPassInfo.Attachments.size();
        RenderPassInfo.Attachments.insert(RenderPassInfo.Attachments.end(), colorAttachments.begin(), colorAttachments.end());

        if (RenderPassInfo.Subpasses.empty())
            RenderPassInfo.Subpasses.resize(1);

        for (int32 i = 0; i < (int32)colorAttachments.size(); ++i)
            RenderPassInfo.Subpasses[0].OutputColorAttachments.push_back(startColorIndex + i);

        // Add output depth attachment
        const int32 startDepthIndex = (int32)RenderPassInfo.Attachments.size();
        RenderPassInfo.Attachments.push_back(depthAttachment);

        RenderPassInfo.Subpasses[0].OutputDepthAttachment = startDepthIndex;
    }

    void SetAttachemnt(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const jAttachment& colorResolveAttachment)
    {
        // Add output color attachment
        const int32 startColorIndex = (int32)RenderPassInfo.Attachments.size();
        RenderPassInfo.Attachments.insert(RenderPassInfo.Attachments.end(), colorAttachments.begin(), colorAttachments.end());

        if (RenderPassInfo.Subpasses.empty())
            RenderPassInfo.Subpasses.resize(1);

        for (int32 i = 0; i < (int32)colorAttachments.size(); ++i)
            RenderPassInfo.Subpasses[0].OutputColorAttachments.push_back(startColorIndex + i);

        // Add output depth attachment
        const int32 startDepthIndex = (int32)RenderPassInfo.Attachments.size();
        RenderPassInfo.Attachments.push_back(depthAttachment);

        RenderPassInfo.Subpasses[0].OutputDepthAttachment = startDepthIndex;

        // Add output resolve attachment
        const int32 startResolveIndex = (int32)RenderPassInfo.Attachments.size();
        RenderPassInfo.Attachments.push_back(colorResolveAttachment);

        RenderPassInfo.Subpasses[0].OutputResolveAttachment = startResolveIndex;
    }

    void SetRenderArea(const Vector2i& offset, const Vector2i& extent)
    {
        RenderOffset = offset;
        RenderExtent = extent;
    }

    jRenderPass(const jRenderPassInfo& renderPassInfo, const Vector2i& offset, const Vector2i& extent)
    {
        RenderPassInfo = renderPassInfo;
        SetRenderArea(offset, extent);
    }

    virtual bool BeginRenderPass(const jCommandBuffer* commandBuffer) { return false; }
    virtual void EndRenderPass() {}

    virtual size_t GetHash() const final;

    virtual void* GetRenderPass() const { return nullptr; }
    virtual void* GetFrameBuffer() const { return nullptr; }

    jRenderPassInfo RenderPassInfo;
    
    //std::vector<jAttachment> ColorAttachments;
    //jAttachment DepthAttachment;
    //jAttachment ColorAttachmentResolve;

    Vector2i RenderOffset;
    Vector2i RenderExtent;
    mutable size_t Hash = 0;
};
