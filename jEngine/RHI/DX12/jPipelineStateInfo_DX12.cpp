#include "pch.h"
#include "jPipelineStateInfo_DX12.h"
#include "jRHI_DX12.h"
#include "jVertexBuffer_DX12.h"
#include "jShader_DX12.h"
#include "dxcapi.h"
#include "jRenderPass_DX12.h"
#include "jShaderBindingLayout_DX12.h"
#include "Shader/jShader.h"

void jSamplerStateInfo_DX12::Initialize()
{
    D3D12_SAMPLER_DESC samplerDesc = {};

    samplerDesc.AddressU = GetDX12TextureAddressMode(AddressU);
    samplerDesc.AddressV = GetDX12TextureAddressMode(AddressV);
    samplerDesc.AddressW = GetDX12TextureAddressMode(AddressW);
    samplerDesc.MinLOD = MinLOD;
    samplerDesc.MaxLOD = MaxLOD;
    samplerDesc.MipLODBias = MipLODBias;
    samplerDesc.MaxAnisotropy = (uint32)MaxAnisotropy;
    if (MaxAnisotropy > 1)
        samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
    else
        samplerDesc.Filter = GetDX12TextureFilter(Minification, Magnification, IsEnableComparisonMode);
    samplerDesc.ComparisonFunc = GetDX12CompareOp(ComparisonFunc);

    check(sizeof(samplerDesc.BorderColor) == sizeof(BorderColor));
    memcpy(samplerDesc.BorderColor, &BorderColor, sizeof(BorderColor));
    
    SamplerSRV = g_rhi_dx12->SamplerDescriptorHeaps.Alloc();

    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);
    g_rhi_dx12->Device->CreateSampler(&samplerDesc, SamplerSRV.CPUHandle);

    ResourceName = jName(ToString().c_str());
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
    if (PipelineType == EPipelineType::Graphics)
        CreateGraphicsPipelineState();
    else if (PipelineType == EPipelineType::Compute)
        CreateComputePipelineState();
    else if (PipelineType == EPipelineType::RayTracing)
        CreateRaytracingPipelineState();
    else
        check(0);
}

void* jPipelineStateInfo_DX12::CreateGraphicsPipelineState()
{
    PipelineState = nullptr;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        
    check(VertexBufferArray.NumOfData > 0);

    std::vector<D3D12_INPUT_ELEMENT_DESC> OutInputElementDescs;
    jVertexBuffer_DX12::CreateVertexInputState(OutInputElementDescs, VertexBufferArray);
    psoDesc.InputLayout.pInputElementDescs = OutInputElementDescs.data();
    psoDesc.InputLayout.NumElements = (uint32)OutInputElementDescs.size();

    // DX12 에서는 한개만 사용할 수 있도록 되어있는데, 변경 예정. 1개로 할지, 여러개로 할지 협의 봐야 함.
    ComPtr<ID3D12RootSignature> RootSignature = jShaderBindingLayout_DX12::CreateRootSignature(ShaderBindingLayoutArray);
    psoDesc.pRootSignature = RootSignature.Get();

    if (GraphicsShader.VertexShader)
    {
        auto VS_Compiled = (jCompiledShader_DX12*)GraphicsShader.VertexShader->GetCompiledShader();
        check(VS_Compiled);
        psoDesc.VS = { .pShaderBytecode = VS_Compiled->ShaderBlob->GetBufferPointer(), .BytecodeLength = VS_Compiled->ShaderBlob->GetBufferSize() };
    }
    if (GraphicsShader.GeometryShader)
    {
        auto GS_Compiled = (jCompiledShader_DX12*)GraphicsShader.GeometryShader->GetCompiledShader();
        check(GS_Compiled);
        psoDesc.GS = { .pShaderBytecode = GS_Compiled->ShaderBlob->GetBufferPointer(), .BytecodeLength = GS_Compiled->ShaderBlob->GetBufferSize() };
    }
    if (GraphicsShader.PixelShader)
    {
        auto PS_Compiled = (jCompiledShader_DX12*)GraphicsShader.PixelShader->GetCompiledShader();
        check(PS_Compiled);
        psoDesc.PS = { .pShaderBytecode = PS_Compiled->ShaderBlob->GetBufferPointer(), .BytecodeLength = PS_Compiled->ShaderBlob->GetBufferSize() };
    }

    psoDesc.RasterizerState = ((jRasterizationStateInfo_DX12*)PipelineStateFixed->RasterizationState)->RasterizeDesc;
    psoDesc.SampleDesc.Count = ((jRasterizationStateInfo_DX12*)PipelineStateFixed->RasterizationState)->MultiSampleDesc.Count;

    jRenderPass_DX12* RenderPassDX12 = (jRenderPass_DX12*)RenderPass;

    // Blending Operation 을 따로 적게 해줄건가? RenderPass 의 현재 지원을 확인해보자.
    for (int32 i = 0; i < (int32)RenderPassDX12->GetRTVFormats().size(); ++i)
    {
        psoDesc.BlendState.RenderTarget[i] = ((jBlendingStateInfo_DX12*)PipelineStateFixed->BlendingState)->BlendDesc;
    }
    psoDesc.DepthStencilState = ((jDepthStencilStateInfo_DX12*)(PipelineStateFixed->DepthStencilState))->DepthStencilStateDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = ((jVertexBuffer_DX12*)VertexBufferArray[0])->GetTopologyTypeOnly();
    psoDesc.NumRenderTargets = (uint32)RenderPassDX12->GetRTVFormats().size();

    const int32 NumOfRTVs = Min((int32)_countof(psoDesc.RTVFormats), (int32)RenderPassDX12->GetRTVFormats().size());
    for (int32 i = 0; i < NumOfRTVs; ++i)
    {
        psoDesc.RTVFormats[i] = RenderPassDX12->GetRTVFormats()[i];
    }
    psoDesc.DSVFormat = RenderPassDX12->GetDSVFormat();

    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);
    if (JFAIL(g_rhi_dx12->Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&PipelineState))))
        return nullptr;

    Viewports.resize(PipelineStateFixed->Viewports.size());
    for (int32 i = 0; i < (int32)PipelineStateFixed->Viewports.size(); ++i)
    {
        const jViewport& Src = PipelineStateFixed->Viewports[i];
        D3D12_VIEWPORT& Dst = Viewports[i];
        Dst.TopLeftX = Src.X;
        Dst.TopLeftY = Src.Y;
        Dst.Width = Src.Width;
        Dst.Height = Src.Height;
        Dst.MinDepth = Src.MinDepth;
        Dst.MaxDepth = Src.MaxDepth;
    }

    Scissors.resize(PipelineStateFixed->Scissors.size());
    for (int32 i = 0; i < (int32)PipelineStateFixed->Scissors.size(); ++i)
    {
        const jScissor& Src = PipelineStateFixed->Scissors[i];
        D3D12_RECT& Dst = Scissors[i];
        Dst.left = Src.Offset.x;
        Dst.right = Src.Offset.x + Src.Extent.x;
        Dst.top = Src.Offset.y;
        Dst.bottom = Src.Offset.y + Src.Extent.y;
    }

    size_t hash = GetHash();
    if (ensure(hash))
    {
        if (GraphicsShader.VertexShader)
            jShader::gConnectedPipelineStateHash[GraphicsShader.VertexShader].push_back(hash);
        if (GraphicsShader.GeometryShader)
            jShader::gConnectedPipelineStateHash[GraphicsShader.GeometryShader].push_back(hash);
        if (GraphicsShader.PixelShader)
            jShader::gConnectedPipelineStateHash[GraphicsShader.PixelShader].push_back(hash);
    }

    return PipelineState.Get();
}

void* jPipelineStateInfo_DX12::CreateComputePipelineState()
{
    PipelineState = nullptr;

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    ComPtr<ID3D12RootSignature> RootSignature = jShaderBindingLayout_DX12::CreateRootSignature(ShaderBindingLayoutArray);
    psoDesc.pRootSignature = RootSignature.Get();
    if (ComputeShader)
    {
        auto CS_Compiled = (jCompiledShader_DX12*)ComputeShader->GetCompiledShader();
        check(CS_Compiled);
        psoDesc.CS = { .pShaderBytecode = CS_Compiled->ShaderBlob->GetBufferPointer(), .BytecodeLength = CS_Compiled->ShaderBlob->GetBufferSize() };
    }
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);
    if (JFAIL(g_rhi_dx12->Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&PipelineState))))
        return nullptr;

    size_t hash = GetHash();
    if (ensure(hash))
    {
        if (ComputeShader)
            jShader::gConnectedPipelineStateHash[ComputeShader].push_back(hash);
    }

    return PipelineState.Get();
}

void* jPipelineStateInfo_DX12::CreateRaytracingPipelineState()
{
    PipelineState = nullptr;

    ID3D12RootSignature* RootSignature = jShaderBindingLayout_DX12::CreateRootSignature(ShaderBindingLayoutArray);

    std::vector<D3D12_EXPORT_DESC> exportDescs;
    exportDescs.reserve(RaytracingShaders.size() * 4);

    std::vector<D3D12_DXIL_LIBRARY_DESC> dxilDescs;
    dxilDescs.reserve(RaytracingShaders.size() * 4);

    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(20);
    auto AddShaderFunc = [&](jShader* InShader, const wchar_t* InEntryPoint)
    {
        D3D12_EXPORT_DESC exportDesc{};
        exportDesc.Name = InEntryPoint;
        exportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
        exportDesc.ExportToRename = nullptr;
        exportDescs.push_back(exportDesc);

        auto CompiledShaderDX12 = (jCompiledShader_DX12*)InShader->GetCompiledShader();

        D3D12_DXIL_LIBRARY_DESC dxilDesc{};
        dxilDesc.DXILLibrary.pShaderBytecode = CompiledShaderDX12->ShaderBlob->GetBufferPointer();
        dxilDesc.DXILLibrary.BytecodeLength = CompiledShaderDX12->ShaderBlob->GetBufferSize();
        dxilDesc.NumExports = 1;
        dxilDesc.pExports = &exportDescs[exportDescs.size() - 1];
        dxilDescs.push_back(dxilDesc);

        D3D12_STATE_SUBOBJECT subobject{};
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        subobject.pDesc = &dxilDescs[dxilDescs.size() - 1];
        subobjects.push_back(subobject);
    };

    // Add Shader
    std::vector<D3D12_HIT_GROUP_DESC> hitgroupDescs;
    hitgroupDescs.reserve(RaytracingShaders.size());
    for (int32 i = 0; i < RaytracingShaders.size(); ++i)
    {
        AddShaderFunc(RaytracingShaders[i].RaygenShader, RaytracingShaders[i].RaygenEntryPoint.c_str());
        AddShaderFunc(RaytracingShaders[i].ClosestHitShader, RaytracingShaders[i].ClosestHitEntryPoint.c_str());
        AddShaderFunc(RaytracingShaders[i].AnyHitShader, RaytracingShaders[i].AnyHitEntryPoint.c_str());
        AddShaderFunc(RaytracingShaders[i].MissShader, RaytracingShaders[i].MissEntryPoint.c_str());

        D3D12_HIT_GROUP_DESC hitGroupDesc{};
        hitGroupDesc.AnyHitShaderImport = RaytracingShaders[i].AnyHitEntryPoint.c_str();
        hitGroupDesc.ClosestHitShaderImport = RaytracingShaders[i].ClosestHitEntryPoint.c_str();
        hitGroupDesc.HitGroupExport = RaytracingShaders[i].HitGroupName.c_str();
        hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitgroupDescs.push_back(hitGroupDesc);

        D3D12_STATE_SUBOBJECT subobject{};
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subobject.pDesc = &hitgroupDescs[hitgroupDescs.size() - 1];
        subobjects.push_back(subobject);
    }

    // Shader Config
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
    {
        shaderConfig.MaxAttributeSizeInBytes = RaytracingPipelineData.MaxAttributeSize;
        shaderConfig.MaxPayloadSizeInBytes = RaytracingPipelineData.MaxPayloadSize;

        D3D12_STATE_SUBOBJECT subobject{};
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        subobject.pDesc = &shaderConfig;
        subobjects.push_back(subobject);
    }

    // Global root signature
    {
        D3D12_STATE_SUBOBJECT subobject{};
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        subobject.pDesc = &RootSignature;
        subobjects.push_back(subobject);
    }

    // Pipeline Config
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
    {
        pipelineConfig.MaxTraceRecursionDepth = RaytracingPipelineData.MaxTraceRecursionDepth;

        D3D12_STATE_SUBOBJECT subobject{};
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        subobject.pDesc = &pipelineConfig;
        subobjects.push_back(subobject);
    }

    // Create pipeline state
    D3D12_STATE_OBJECT_DESC stateObjectDesc;
    stateObjectDesc.NumSubobjects = (uint32)subobjects.size();
    stateObjectDesc.pSubobjects = subobjects.data();
    stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    ComPtr<ID3D12StateObject> m_dxrStateObject;
    if (JFAIL(g_rhi_dx12->Device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_dxrStateObject))))
        return nullptr;

    size_t hash = GetHash();
    if (ensure(hash))
    {
        for (int32 i = 0; i < (int32)RaytracingShaders.size(); ++i)
        {
            if (RaytracingShaders[i].RaygenShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].RaygenShader].push_back(hash);
            if (RaytracingShaders[i].ClosestHitShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].ClosestHitShader].push_back(hash);
            if (RaytracingShaders[i].AnyHitShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].AnyHitShader].push_back(hash);
            if (RaytracingShaders[i].MissShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].MissShader].push_back(hash);
        }
    }

    return PipelineState.Get();
}

void jPipelineStateInfo_DX12::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    //check(vkPipeline);
    //if (IsGraphics)
    //    vkCmdBindPipeline((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
    //else
    //    vkCmdBindPipeline((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline);

    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

    Bind(CommandBuffer_DX12);
}

void jPipelineStateInfo_DX12::Bind(jCommandBuffer_DX12* InCommandList) const
{
    check(InCommandList->CommandList);
    check(PipelineState);
    InCommandList->CommandList->SetPipelineState(PipelineState.Get());

    InCommandList->CommandList->RSSetViewports((uint32)Viewports.size(), Viewports.data());
    InCommandList->CommandList->RSSetScissorRects((uint32)Scissors.size(), Scissors.data());
}
