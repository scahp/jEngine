#include "pch.h"
#include "jPipelineStateInfo_DX12.h"
#include "jRHI_DX12.h"

void jSamplerStateInfo_DX12::Initialize()
{
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = GetDX12TextureAddressMode(AddressU);
    samplerDesc.AddressV = GetDX12TextureAddressMode(AddressV);
    samplerDesc.AddressW = GetDX12TextureAddressMode(AddressW);
    samplerDesc.MinLOD = MinLOD;
    samplerDesc.MaxLOD = MaxLOD;
    samplerDesc.MipLODBias = MipLODBias;
    samplerDesc.MaxAnisotropy = (uint32)MaxAnisotropy;
    samplerDesc.ComparisonFunc = GetDX12CompareOp(ComparisonFunc);

    check(sizeof(samplerDesc.BorderColor) == sizeof(BorderColor));
    memcpy(samplerDesc.BorderColor, &BorderColor, sizeof(BorderColor));
    
    SamplerSRV = g_rhi_dx12->SamplerDescriptorHeaps.Alloc();

    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);
    g_rhi_dx12->Device->CreateSampler(&samplerDesc, SamplerSRV.CPUHandle);
}

void jSamplerStateInfo_DX12::Release()
{
    SamplerSRV.Free();
}

void jPipelineStateInfo_DX12::Release()
{
    //if (vkPipeline)
    //{
    //    vkDestroyPipeline(g_rhi_vk->Device, vkPipeline, nullptr);
    //    vkPipeline = nullptr;
    //}
    //vkPipelineLayout = nullptr;
}

void jPipelineStateInfo_DX12::Initialize()
{
    if (IsGraphics)
        CreateGraphicsPipelineState();
    else
        CreateComputePipelineState();
}

void* jPipelineStateInfo_DX12::CreateGraphicsPipelineState()
{
    return nullptr;
}

void* jPipelineStateInfo_DX12::CreateComputePipelineState()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    //psoDesc.InputLayout = { .pInputElementDescs = VSInputElementDesc, .NumElements = _countof(VSInputElementDesc) };
    //psoDesc.pRootSignature = TestShaderBindingInstance->GetRootSignature();
    //psoDesc.VS = { .pShaderBytecode = VSShaderBlob->GetBufferPointer(), .BytecodeLength = VSShaderBlob->GetBufferSize() };
    //psoDesc.PS = { .pShaderBytecode = PSShaderBlob->GetBufferPointer(), .BytecodeLength = PSShaderBlob->GetBufferSize() };

    //const jGraphicsPipelineShader GraphicsShader;
    //const jShader* ComputeShader = nullptr;
    //const jRenderPass* RenderPass = nullptr;
    //jVertexBufferArray VertexBufferArray;
    //jShaderBindingsLayoutArray ShaderBindingLayoutArray;
    //const jPushConstant* PushConstant;
    //const jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
    //int32 SubpassIndex = 0;

    psoDesc.RasterizerState = ((jRasterizationStateInfo_DX12*)PipelineStateFixed->RasterizationState)->RasterizeDesc;
    psoDesc.BlendState = ((jBlendingStateInfo_DX12*)PipelineStateFixed->BlendingState)->BlendDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.DepthStencilState = ((jDepthStencilStateInfo_DX12*)PipelineStateFixed->DepthStencilState)->DepthStencilStateDesc;
    psoDesc.SampleDesc.Count = ((jRasterizationStateInfo_DX12*)PipelineStateFixed->RasterizationState)->MultiSampleDesc.Count;

    int32 ColorAttachmentCountInSubpass = 0;
    check(RenderPass->RenderPassInfo.Subpasses.size() > SubpassIndex);
    const jSubpass& SelectedSubpass = RenderPass->RenderPassInfo.Subpasses[SubpassIndex];
    for (int32 i = 0; i < (int32)SelectedSubpass.OutputColorAttachments.size(); ++i)
    {
        const int32 AttchmentIndex = SelectedSubpass.OutputColorAttachments[i];
        const jAttachment& Attachment = RenderPass->RenderPassInfo.Attachments[AttchmentIndex];
        const bool IsColorAttachment = !Attachment.IsDepthAttachment() &&
            !Attachment.IsResolveAttachment();
        if (IsColorAttachment)
        {
            psoDesc.RTVFormats[ColorAttachmentCountInSubpass] = GetDX12TextureFormat(Attachment.RenderTargetPtr->Info.Format);
            ++ColorAttachmentCountInSubpass;
        }
        else if (RenderPass->RenderPassInfo.Attachments[AttchmentIndex].IsDepthAttachment())
        {
            psoDesc.DSVFormat = GetDX12TextureFormat(Attachment.RenderTargetPtr->Info.Format);
        }
    }
    psoDesc.NumRenderTargets = ColorAttachmentCountInSubpass;

    if (JFAIL(g_rhi_dx12->Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&PipelineState))))
        return nullptr;

    return PipelineState.Get();
}

void jPipelineStateInfo_DX12::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    //check(vkPipeline);
    //if (IsGraphics)
    //    vkCmdBindPipeline((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
    //else
    //    vkCmdBindPipeline((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline);
}
