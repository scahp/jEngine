#include "pch.h"
#include "jPipelineStateInfo_DX12.h"

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
    samplerDesc.MaxAnisotropy = MaxAnisotropy;
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

