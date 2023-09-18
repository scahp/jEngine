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

void jRasterizationStateInfo_DX12::Initialize()
{
    RasterizeDesc.FillMode = GetDX12PolygonMode(PolygonMode);
    RasterizeDesc.CullMode = GetDX12CullMode(CullMode);
    RasterizeDesc.FrontCounterClockwise = FrontFace == EFrontFace::CCW;
    RasterizeDesc.DepthBias = (int32)(DepthBiasEnable ? DepthBiasConstantFactor : 0);
    RasterizeDesc.DepthBiasClamp = DepthBiasClamp;
    RasterizeDesc.SlopeScaledDepthBias = DepthBiasSlopeFactor;
    RasterizeDesc.DepthClipEnable = DepthClampEnable;
    RasterizeDesc.MultisampleEnable = (int32)SampleCount > 1;

    //RasterizeDesc.AntialiasedLineEnable;
    //RasterizeDesc.ForcedSampleCount;
    //RasterizeDesc.ConservativeRaster;

    MultiSampleDesc.Count = (int32)SampleCount;
}

void jStencilOpStateInfo_DX12::Initialize()
{
    StencilOpStateDesc.StencilDepthFailOp = GetDX12StencilOp(DepthFailOp);
    StencilOpStateDesc.StencilFailOp = GetDX12StencilOp(FailOp);
    StencilOpStateDesc.StencilFunc = GetDX12CompareOp(CompareOp);
    StencilOpStateDesc.StencilPassOp = GetDX12StencilOp(PassOp);
}

void jDepthStencilStateInfo_DX12::Initialize()
{
    DepthStencilStateDesc.DepthEnable = DepthTestEnable;
    DepthStencilStateDesc.DepthWriteMask = DepthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    DepthStencilStateDesc.DepthFunc = GetDX12CompareOp(DepthCompareOp);
    DepthStencilStateDesc.StencilEnable = StencilTestEnable;
    //DepthStencilStateDesc.StencilReadMask;
    //DepthStencilStateDesc.StencilWriteMask;
    if (Front)
        DepthStencilStateDesc.FrontFace = ((jStencilOpStateInfo_DX12*)Front)->StencilOpStateDesc;

    if (Back)
        DepthStencilStateDesc.BackFace = ((jStencilOpStateInfo_DX12*)Back)->StencilOpStateDesc;

    // MinDepthBounds;
    // MaxDepthBounds;
}

void jBlendingStateInfo_DX12::Initialize()
{
    BlendDesc.BlendEnable = BlendEnable;
    BlendDesc.SrcBlend = GetDX12BlendFactor(Src);
    BlendDesc.DestBlend = GetDX12BlendFactor(Dest);
    BlendDesc.BlendOp = GetDX12BlendOp(BlendOp);
    BlendDesc.SrcBlendAlpha = GetDX12BlendFactor(SrcAlpha);
    BlendDesc.DestBlendAlpha = GetDX12BlendFactor(DestAlpha);
    BlendDesc.BlendOpAlpha = GetDX12BlendOp(AlphaBlendOp);
    BlendDesc.RenderTargetWriteMask = GetDX12ColorMask(ColorWriteMask);

    //BlendDesc.LogicOpEnable;
    //BlendDesc.LogicOp;
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

    psoDesc.RasterizerState = ((jRasterizationStateInfo_DX12*)PipelineStateFixed->RasterizationState)->RasterizeDesc;
    for (int32 i = 0; i < psoDesc.NumRenderTargets; ++i)
    {
        psoDesc.BlendState.RenderTarget[i] = ((jBlendingStateInfo_DX12*)PipelineStateFixed->BlendingState)->BlendDesc;
    }
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.DepthStencilState = ((jDepthStencilStateInfo_DX12*)PipelineStateFixed->DepthStencilState)->DepthStencilStateDesc;
    psoDesc.SampleDesc.Count = ((jRasterizationStateInfo_DX12*)PipelineStateFixed->RasterizationState)->MultiSampleDesc.Count;

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
