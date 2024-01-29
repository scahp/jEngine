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

    virtual bool BeginRenderPass(const jCommandBuffer* commandBuffer) override
    {
        if (!ensure(commandBuffer))
            return false;

        CommandBuffer = commandBuffer;

        check(FrameBuffer);
        RenderPassBeginInfo.framebuffer = FrameBuffer;

        // 커맨드를 기록하는 명령어는 prefix로 모두 vkCmd 가 붙으며, 리턴값은 void 로 에러 핸들링은 따로 안함.
        // VK_SUBPASS_CONTENTS_INLINE : 렌더 패스 명령이 Primary 커맨드 버퍼에 포함되며, Secondary 커맨드 버퍼는 실행되지 않는다.
        // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : 렌더 패스 명령이 Secondary 커맨드 버퍼에서 실행된다.
        vkCmdBeginRenderPass((VkCommandBuffer)commandBuffer->GetHandle(), &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        return true;
    }

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
