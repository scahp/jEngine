#pragma once
#include "../jRenderPass.h"

class jRenderPass_DX12 : public jRenderPass
{
public:
    using jRenderPass::jRenderPass;

    virtual ~jRenderPass_DX12()
    {
        Release();
    }

    void Initialize();
    bool CreateRenderPass();
    void Release();

    //virtual void* GetRenderPass() const override { return RenderPass; }
    //FORCEINLINE const VkRenderPass& GetRenderPassRaw() const { return RenderPass; }
    //virtual void* GetFrameBuffer() const override { return FrameBuffer; }

    virtual bool BeginRenderPass(const jCommandBuffer* commandBuffer) override;

    // 제거해야 함. jCommandBuffer_DX12 와 jCommandBuffer 관계 정리 마치고.
    bool BeginRenderPass(const jCommandBuffer_DX12* commandBuffer, D3D12_CPU_DESCRIPTOR_HANDLE InTempRTV)
    {
        if (!ensure(commandBuffer))
            return false;

        //// RenderPass 를 매 프레임 새로 갱신하게 만들게 되면, 이 코드를 제거하고 아래 코드를 사용.
        //commandBuffer->CommandList->OMSetRenderTargets(1, &InTempRTV, false, nullptr);
        //const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        //commandBuffer->CommandList->ClearRenderTargetView(InTempRTV, clearColor, 0, nullptr);
        //return true;

        commandBuffer->CommandList->OMSetRenderTargets((uint32)RTVCPUHandles.size(), &RTVCPUHandles[0], false, &DSVCPUDHandle);

        for (int32 i = 0; i < RTVClears.size(); ++i)
        {
            if (RTVClears[i].GetType() != ERTClearType::Color)
                continue;

            commandBuffer->CommandList->ClearRenderTargetView(RTVCPUHandles[i], RTVClears[i].GetCleraColor(), 0, nullptr);
        }

        
        if (DSVClear.GetType() == ERTClearType::DepthStencil)
        {
            if (DSVDepthClear || DSVStencilClear)
            {
                D3D12_CLEAR_FLAGS DSVClearFlags = (D3D12_CLEAR_FLAGS)0;
                if (DSVDepthClear)
                    DSVClearFlags |= D3D12_CLEAR_FLAG_DEPTH;
                
                if (DSVStencilClear)
                    DSVClearFlags |= D3D12_CLEAR_FLAG_STENCIL;

                commandBuffer->CommandList->ClearDepthStencilView(DSVCPUDHandle, DSVClearFlags, DSVClear.GetCleraDepth(), (uint8)DSVClear.GetCleraStencil(), 0, nullptr);
            }
        }

        return true;
    }

    virtual void EndRenderPass() override;

    const std::vector<DXGI_FORMAT>& GetRTVFormats() const { return RTVFormats; }
    DXGI_FORMAT GetDSVFormat() const { return DSVFormat; }

private:
    void SetFinalLayoutToAttachment(const jAttachment& attachment) const;

private:
    const jCommandBuffer_DX12* CommandBuffer = nullptr;

    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> RTVCPUHandles;
    D3D12_CPU_DESCRIPTOR_HANDLE DSVCPUDHandle = {};

    std::vector<jRTClearValue> RTVClears;
    jRTClearValue DSVClear = jRTClearValue(1.0f, 0);
    bool DSVDepthClear = false;
    bool DSVStencilClear = false;

    std::vector<DXGI_FORMAT> RTVFormats;
    DXGI_FORMAT DSVFormat = DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
};
