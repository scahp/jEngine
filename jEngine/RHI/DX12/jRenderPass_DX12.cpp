﻿#include "pch.h"
#include "jRenderPass_DX12.h"
#include "jRHIType_DX12.h"
#include "jTexture_DX12.h"

void jRenderPass_DX12::Release()
{
}

bool jRenderPass_DX12::BeginRenderPass(const jCommandBuffer* commandBuffer)
{
    if (!ensure(commandBuffer))
        return false;

#if USE_RESOURCE_BARRIER_BATCHER
    g_rhi->GetGlobalBarrierBatcher()->Flush(commandBuffer);
    commandBuffer->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    CommandBuffer = (const jCommandBuffer_DX12*)commandBuffer;

    if (RTVCPUHandles.size() > 0)
        CommandBuffer->CommandList->OMSetRenderTargets((uint32)RTVCPUHandles.size(), &RTVCPUHandles[0], false, (DSVCPUDHandle.ptr ? &DSVCPUDHandle : nullptr));
    else
        CommandBuffer->CommandList->OMSetRenderTargets(0, nullptr, false, (DSVCPUDHandle.ptr ? &DSVCPUDHandle : nullptr));

    for (int32 i = 0; i < RTVClears.size(); ++i)
    {
        if (RTVClears[i].GetType() != ERTClearType::Color)
            continue;

        CommandBuffer->CommandList->ClearRenderTargetView(RTVCPUHandles[i], RTVClears[i].GetCleraColor(), 0, nullptr);
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

            CommandBuffer->CommandList->ClearDepthStencilView(DSVCPUDHandle, DSVClearFlags, DSVClear.GetCleraDepth(), (uint8)DSVClear.GetCleraStencil(), 0, nullptr);
        }
    }

    return true;
}

void jRenderPass_DX12::EndRenderPass()
{
}

void jRenderPass_DX12::SetFinalLayoutToAttachment(const jAttachment& attachment) const
{
}

void jRenderPass_DX12::Initialize()
{
    CreateRenderPass();
}

bool jRenderPass_DX12::CreateRenderPass()
{   
    // Create RenderPass
    {
        for (int32 i = 0; i < (int32)RenderPassInfo.Attachments.size(); ++i)
        {
            const jAttachment& attachment = RenderPassInfo.Attachments[i];
            check(attachment.IsValid());

            const auto& RTInfo = attachment.RenderTargetPtr.lock()->Info;
            const bool HasClear = (attachment.LoadStoreOp == EAttachmentLoadStoreOp::CLEAR_STORE ||
                attachment.LoadStoreOp == EAttachmentLoadStoreOp::CLEAR_DONTCARE);
            jTexture_DX12* TextureDX12 = (jTexture_DX12*)attachment.RenderTargetPtr.lock()->GetTexture();

            if (attachment.IsDepthAttachment())
            {
                DSVFormat = GetDX12TextureFormat(RTInfo.Format);
                DSVClear = attachment.RTClearValue;

                DSVDepthClear = HasClear;
                DSVStencilClear = (attachment.StencilLoadStoreOp == EAttachmentLoadStoreOp::CLEAR_STORE ||
                    attachment.StencilLoadStoreOp == EAttachmentLoadStoreOp::CLEAR_DONTCARE);

                DSVCPUDHandle = TextureDX12->DSV.CPUHandle;
            }
            else
            {
                if (HasClear)
                {
                    RTVClears.push_back(attachment.RTClearValue);
                }
                else
                {
                    RTVClears.push_back(jRTClearValue::Invalid);
                }
                RTVCPUHandles.push_back(TextureDX12->RTV.CPUHandle);
                RTVFormats.push_back(GetDX12TextureFormat(RTInfo.Format));
            }
        }
    }

    return true;
}
