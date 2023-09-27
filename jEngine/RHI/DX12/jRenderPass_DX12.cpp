#include "pch.h"
#include "jRenderPass_DX12.h"
#include "jRHIType_DX12.h"
#include "jTexture_DX12.h"

void jRenderPass_DX12::Release()
{
    //if (FrameBuffer)
    //{
    //    vkDestroyFramebuffer(g_rhi_vk->Device, FrameBuffer, nullptr);
    //    FrameBuffer = nullptr;
    //}

    //if (RenderPass)
    //{
    //    vkDestroyRenderPass(g_rhi_vk->Device, RenderPass, nullptr);
    //    RenderPass = nullptr;
    //}
}

bool jRenderPass_DX12::BeginRenderPass(const jCommandBuffer* commandBuffer)
{
    if (!ensure(commandBuffer))
        return false;

    CommandBuffer = (const jCommandBuffer_DX12*)commandBuffer;

    if (RTVCPUHandles.size() > 0)
        CommandBuffer->CommandList->OMSetRenderTargets((uint32)RTVCPUHandles.size(), &RTVCPUHandles[0], false, &DSVCPUDHandle);
    else
        CommandBuffer->CommandList->OMSetRenderTargets(0, nullptr, false, &DSVCPUDHandle);

    for (int32 i = 0; i < RTVClears.size(); ++i)
    {
        if (!RTVClears[i].bClear)
            continue;

        CommandBuffer->CommandList->ClearRenderTargetView(RTVCPUHandles[i], RTVClears[i].Color.v, 0, nullptr);
    }


    if (DSVClear.bClear)
    {
        if (DSVDepthClear || DSVStencilClear)
        {
            D3D12_CLEAR_FLAGS DSVClearFlags = (D3D12_CLEAR_FLAGS)0;
            if (DSVDepthClear)
                DSVClearFlags |= D3D12_CLEAR_FLAG_DEPTH;

            if (DSVStencilClear)
                DSVClearFlags |= D3D12_CLEAR_FLAG_STENCIL;

            const float DepthClear = DSVClear.DepthStencil.x;
            const float StencilClear = DSVClear.DepthStencil.y;
            CommandBuffer->CommandList->ClearDepthStencilView(DSVCPUDHandle, DSVClearFlags, DepthClear, (uint8)StencilClear, 0, nullptr);
        }
    }

    return true;
}

void jRenderPass_DX12::EndRenderPass()
{
    //ensure(CommandBuffer);

    //// Finishing up
    //vkCmdEndRenderPass((VkCommandBuffer)CommandBuffer->GetHandle());

    //// Apply layout to attachments
    //for(jAttachment& iter : RenderPassInfo.Attachments)
    //{
    //    check(iter.IsValid());
    //    SetFinalLayoutToAttachment(iter);
    //}

    //CommandBuffer = nullptr;
}

void jRenderPass_DX12::SetFinalLayoutToAttachment(const jAttachment& attachment) const
{
    //check(attachment.RenderTargetPtr);
    //jTexture_DX12* texture_vk = (jTexture_DX12*)attachment.RenderTargetPtr->GetTexture();
    //texture_vk->Layout = attachment.FinalLayout;
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

            const auto& RTInfo = attachment.RenderTargetPtr->Info;
            const bool HasClear = (attachment.LoadStoreOp == EAttachmentLoadStoreOp::CLEAR_STORE ||
                attachment.LoadStoreOp == EAttachmentLoadStoreOp::CLEAR_DONTCARE);
            jTexture_DX12* TextureDX12 = (jTexture_DX12*)attachment.RenderTargetPtr->GetTexture();

            if (attachment.IsDepthAttachment())
            {
                DSVFormat = GetDX12TextureFormat(RTInfo.Format);
                DSVClear.bClear = HasClear;
                DSVClear.DepthStencil = attachment.ClearDepth;

                DSVDepthClear = HasClear;
                DSVStencilClear = (attachment.StencilLoadStoreOp == EAttachmentLoadStoreOp::CLEAR_STORE ||
                    attachment.StencilLoadStoreOp == EAttachmentLoadStoreOp::CLEAR_DONTCARE);

                DSVCPUDHandle = TextureDX12->DSV.CPUHandle;
            }
            else
            {                
                RTVCPUHandles.push_back(TextureDX12->RTV.CPUHandle);
                RTVClears.push_back({.bClear = HasClear, .Color = attachment.ClearColor});

                RTVFormats.push_back(GetDX12TextureFormat(RTInfo.Format));
            }
        }
    }

    return true;
}
