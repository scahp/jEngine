#pragma once
#include "../jRenderPass.h"

class jRenderPass_Vulkan : public jRenderPass
{
public:
    using jRenderPass::jRenderPass;

    virtual ~jRenderPass_Vulkan()
    {
        Release();
    }

    void Initialize();
    bool CreateRenderPass();
    void Release();

    virtual void* GetRenderPass() const override { return RenderPass; }
    FORCEINLINE const VkRenderPass& GetRenderPassRaw() const { return RenderPass; }
    virtual void* GetFrameBuffer() const override { return FrameBuffer; }
	virtual bool IsInvalidated() const override { return !IsValidRenderTargets(); }
    virtual bool IsValidRenderTargets() const override;

    virtual bool BeginRenderPass(const jCommandBuffer* commandBuffer) override;

    virtual void EndRenderPass() override;

private:
    void SetFinalLayoutToAttachment(const jAttachment& attachment) const;

private:
    const jCommandBuffer* CommandBuffer = nullptr;

    VkRenderPassBeginInfo RenderPassBeginInfo{};
    std::vector<VkClearValue> ClearValues;
    VkRenderPass RenderPass = nullptr;
    VkFramebuffer FrameBuffer = nullptr;
};
