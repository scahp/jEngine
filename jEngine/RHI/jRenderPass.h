#pragma once

struct jAttachment
{
    jAttachment() = default;
    jAttachment(const std::shared_ptr<jRenderTarget>& InRTPtr
        , EAttachmentLoadStoreOp InLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
        , EAttachmentLoadStoreOp InStencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
        , Vector4 InClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f)
        , Vector2 InClearDepth = Vector2(1.0f, 0.0f)
        , bool InTransitToShaderReadAtFinal = false)
        : RenderTargetPtr(InRTPtr), LoadStoreOp(InLoadStoreOp), StencilLoadStoreOp(InStencilLoadStoreOp)
        , ClearColor(InClearColor), ClearDepth(InClearDepth), TransitToShaderReadAtFinal(InTransitToShaderReadAtFinal)
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

    bool TransitToShaderReadAtFinal = false;

    size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = 0;
        Hash ^= CityHash64((const char*)&LoadStoreOp, sizeof(LoadStoreOp));
        Hash ^= CityHash64((const char*)&StencilLoadStoreOp, sizeof(StencilLoadStoreOp));
        Hash ^= CityHash64((const char*)&ClearColor, sizeof(ClearColor));
        Hash ^= CityHash64((const char*)&ClearDepth, sizeof(ClearDepth));
        Hash ^= CityHash64((const char*)&TransitToShaderReadAtFinal, sizeof(TransitToShaderReadAtFinal));
        return Hash;
    }

    mutable size_t Hash = 0;
};

class jRenderPass
{
public:
    virtual ~jRenderPass() {}

    jRenderPass() = default;
    jRenderPass(const jAttachment* colorAttachment, const jAttachment* depthAttachment, const jAttachment* colorResolveAttachment, const Vector2i& offset, const Vector2i& extent)
    {
        SetAttachemnt(colorAttachment, depthAttachment, colorResolveAttachment);
        SetRenderArea(offset, extent);
    }

    void SetAttachemnt(const jAttachment* colorAttachment, const jAttachment* depthAttachment, const jAttachment* colorResolveAttachment)
    {
        if (colorAttachment)
            ColorAttachments.push_back(colorAttachment);

        DepthAttachment = depthAttachment;
        ColorAttachmentResolve = colorResolveAttachment;
    }

    void SetRenderArea(const Vector2i& offset, const Vector2i& extent)
    {
        RenderOffset = offset;
        RenderExtent = extent;
    }

    virtual bool BeginRenderPass(const jCommandBuffer* commandBuffer) { return false; }
    virtual void EndRenderPass() {}

    virtual size_t GetHash() const;

    virtual void* GetRenderPass() const { return nullptr; }
    virtual void* GetFrameBuffer() const { return nullptr; }

    std::vector<const jAttachment*> ColorAttachments;
    const jAttachment* DepthAttachment = nullptr;
    const jAttachment* ColorAttachmentResolve = nullptr;
    Vector2i RenderOffset;
    Vector2i RenderExtent;
    mutable size_t Hash = 0;
};
