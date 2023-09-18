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

            if (attachment.IsDepthAttachment())
            {
                DSVFormat = GetDX12TextureFormat(RTInfo.Format);
                DSVClear.bClear = HasClear;
                DSVClear.DepthStencil = attachment.ClearDepth;

                DSVDepthClear = HasClear;
                DSVStencilClear = (attachment.StencilLoadStoreOp == EAttachmentLoadStoreOp::CLEAR_STORE ||
                    attachment.StencilLoadStoreOp == EAttachmentLoadStoreOp::CLEAR_DONTCARE);
            }
            else
            {
                RTVClears.push_back({.bClear = HasClear, .Color = attachment.ClearColor});

                RTVFormats.push_back(GetDX12TextureFormat(RTInfo.Format));
            }
        }
    }

    return true;
}
