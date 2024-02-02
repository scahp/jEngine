#pragma once

struct jAttachment
{
    jAttachment() = default;
    jAttachment(const std::weak_ptr<jRenderTarget>& InRTPtr
        , EAttachmentLoadStoreOp InLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
        , EAttachmentLoadStoreOp InStencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
        , jRTClearValue RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f)
        , EResourceLayout InInitialLayout = EResourceLayout::UNDEFINED
        , EResourceLayout InFinalLayout = EResourceLayout::SHADER_READ_ONLY
        , bool InIsResolveAttachment = false
    )
        : RenderTargetPtr(InRTPtr), LoadStoreOp(InLoadStoreOp), StencilLoadStoreOp(InStencilLoadStoreOp)
        , RTClearValue(RTClearValue), InitialLayout(InInitialLayout), FinalLayout(InFinalLayout)
        , bResolveAttachment(InIsResolveAttachment)
    {}

    std::weak_ptr<jRenderTarget> RenderTargetPtr;

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

    jRTClearValue RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);

    EResourceLayout InitialLayout = EResourceLayout::UNDEFINED;
    EResourceLayout FinalLayout = EResourceLayout::SHADER_READ_ONLY;

    bool bResolveAttachment = false;

    FORCEINLINE bool IsValid() const
    {
        if (!RenderTargetPtr.expired())
            return true;
        return false;
    }

    FORCEINLINE size_t GetHash() const
    {
        if (Hash)
            return Hash;

        if (RenderTargetPtr.expired())
            return 0;

        Hash = GETHASH_FROM_INSTANT_STRUCT((RenderTargetPtr.expired() ? 0 : RenderTargetPtr.lock()->GetHash())
            , LoadStoreOp, StencilLoadStoreOp, RTClearValue, InitialLayout, FinalLayout);
        return Hash;
    }

    FORCEINLINE bool IsDepthAttachment() const
    {
        check(!RenderTargetPtr.expired());
        return IsDepthFormat(RenderTargetPtr.lock()->Info.Format);
    }

    FORCEINLINE bool IsResolveAttachment() const
    {
        return bResolveAttachment;
    }

    mutable size_t Hash = 0;
};

struct jSubpass
{
    void Initialize(int32 InSourceSubpassIndex, int32 InDestSubpassIndex, EPipelineStageMask InAttachmentProducePipelineBit, EPipelineStageMask InAttachmentComsumePipelineBit)
    {
        SourceSubpassIndex = InSourceSubpassIndex;
        DestSubpassIndex = InDestSubpassIndex;
        AttachmentProducePipelineBit = InAttachmentProducePipelineBit;
        AttachmentComsumePipelineBit = InAttachmentComsumePipelineBit;
    }

    std::vector<int32> InputAttachments;

    std::vector<int32> OutputColorAttachments;
    std::optional<int32> OutputDepthAttachment;
    std::optional<int32> OutputResolveAttachment;

    // If both SourceSubpass and DstSubpass of all subpasses are -1, subpasses will be executed in order
    int32 SourceSubpassIndex = -1;
    int32 DestSubpassIndex = -1;

    // Default is the most strong pipeline stage mask
    EPipelineStageMask AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;      // The pipeline which attachments would use for this subpass
    EPipelineStageMask AttachmentComsumePipelineBit = EPipelineStageMask::FRAGMENT_SHADER_BIT;              // The pipeline which attachments would use for next subapss

    bool IsSubpassForExecuteInOrder() const
    {
        if ((SourceSubpassIndex == -1) && (DestSubpassIndex == -1))
            return true;

        if (SourceSubpassIndex != -1 && DestSubpassIndex != -1)
            return false;

        // Subpass indices have to be either -1 for all or not for all.
        check(0);
        return false;
    }

    size_t GetHash() const
    {
        size_t Hash = 0;
        if (InputAttachments.size() > 0)
            Hash = XXH64(InputAttachments.data(), InputAttachments.size() * sizeof(int32), Hash);
        if (OutputColorAttachments.size() > 0)
            Hash = XXH64(OutputColorAttachments.data(), OutputColorAttachments.size() * sizeof(int32), Hash);
        if (OutputDepthAttachment)
            Hash = XXH64(&OutputDepthAttachment.value(), sizeof(int32), Hash);
        if (OutputResolveAttachment)
            Hash = XXH64(&OutputResolveAttachment.value(), sizeof(int32), Hash);
        Hash = XXH64(&SourceSubpassIndex, sizeof(int32), Hash);
        Hash = XXH64(&DestSubpassIndex, sizeof(int32), Hash);
        Hash = XXH64(&AttachmentProducePipelineBit, sizeof(EPipelineStageMask), Hash);
        Hash = XXH64(&AttachmentComsumePipelineBit, sizeof(EPipelineStageMask), Hash);
        return Hash;
    }
};

struct jRenderPassInfo
{
    std::vector<jAttachment> Attachments;
    jAttachment ResolveAttachment;
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
            Hash = XXH64(iter.GetHash(), Hash);
        }
        for (const auto& iter : Subpasses)
        {
            Hash = XXH64(iter.GetHash(), Hash);
        }
        return Hash;
    }

    // If both SourceSubpass and DstSubpass of all subpasses are -1, subpasses will be executed in order
    bool IsSubpassForExecuteInOrder() const
    {
        check(Subpasses.size());

        int32 i = 0;
        bool isSubpassForExecuteInOrder = Subpasses[i++].IsSubpassForExecuteInOrder();
        for (;i<(int32)Subpasses.size();++i)
        {
            // All isSubpassForExecuteInOrder of subpasses must be same.
            check(isSubpassForExecuteInOrder == Subpasses[i].IsSubpassForExecuteInOrder());

            if (isSubpassForExecuteInOrder != Subpasses[i].IsSubpassForExecuteInOrder())
                return false;
        }
        return isSubpassForExecuteInOrder;
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
    virtual bool IsInvalidated() const { return false; }
    virtual bool IsValidRenderTargets() const { return true; }

    jRenderPassInfo RenderPassInfo;
    
    //std::vector<jAttachment> ColorAttachments;
    //jAttachment DepthAttachment;
    //jAttachment ColorAttachmentResolve;

    Vector2i RenderOffset;
    Vector2i RenderExtent;
    mutable size_t Hash = 0;
};
