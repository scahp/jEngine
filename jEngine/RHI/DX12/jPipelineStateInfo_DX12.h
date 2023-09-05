#pragma once
#include "jDescriptorHeap_DX12.h"
#include "../jPipelineStateInfo.h"

struct jSamplerStateInfo_DX12 : public jSamplerStateInfo
{
    jSamplerStateInfo_DX12() = default;
    jSamplerStateInfo_DX12(const jSamplerStateInfo& state) : jSamplerStateInfo(state) {}
    virtual ~jSamplerStateInfo_DX12()
    {
        Release();
    }
    virtual void Initialize() override;
    void Release();

    virtual void* GetHandle() const { return (void*)&SamplerDesc; }

    D3D12_SAMPLER_DESC SamplerDesc;
    jDescriptor_DX12 SamplerSRV;
};

