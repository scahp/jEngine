#include "pch.h"
#include "jShaderBindingInstance_DX12.h"
#include "jShaderBindingsLayout_DX12.h"
#include "jUniformBufferBlock_DX12.h"
#include "jTexture_DX12.h"
#include "jBuffer_DX12.h"
#include "../jPipelineStateInfo.h"
#include "jPipelineStateInfo_DX12.h"

void jShaderBindingInstance_DX12::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    UpdateShaderBindings(InShaderBindingArray);
}

void jShaderBindingInstance_DX12::UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray)
{
    // online descriptor set 에 디스크립터 복사하자. CopySimpleDescriptor
    // descriptor layout 찾아서 바인딩 해주는 것으로 하자.
    // layout 으로 부터 복사해서 실제로 루트 시그니처를 만들어야 함? 이 부분을 고민해보자.

    check(ShaderBindingLayout);
    check(ShaderBindingLayout->GetShaderBindingsLayout().NumOfData == InShaderBindingArray.NumOfData);

    Descriptors.clear();
    SamplerDescriptors.clear();
    RootParameterInlines.clear();

    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
    {
        const jShaderBinding* ShaderBinding = InShaderBindingArray[i];
        check(ShaderBinding);
        check(ShaderBinding->Resource);

        switch (ShaderBinding->BindingType)
        {
        case EShaderBindingType::UNIFORMBUFFER:
        case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
        {
            jUniformBufferBlock_DX12* UniformBuffer = (jUniformBufferBlock_DX12*)ShaderBinding->Resource->GetResource();
            check(UniformBuffer->GetBuffer());

            if (ShaderBinding->IsInline)
            {
                RootParameterInlines.push_back(std::make_pair(jInlineRootParamType::CBV, UniformBuffer->GetGPUAddress()));
            }
            else
            {
                Descriptors.push_back(UniformBuffer->GetCBV());
            }            
            break;
        }
        case EShaderBindingType::TEXTURE_SAMPLER_SRV:
        case EShaderBindingType::TEXTURE_SRV:
        {
            jTexture_DX12* Tex = (jTexture_DX12*)ShaderBinding->Resource->GetResource();
            Descriptors.push_back(Tex->SRV);
            break;
        }
        case EShaderBindingType::TEXTURE_ARRAY_SRV:
        {
            jTexture_DX12** Tex = (jTexture_DX12**)ShaderBinding->Resource->GetResource();
            for(int32 i=0;i< ShaderBinding->Resource->NumOfResource();++i)
            {
                check(Tex[i]);
                Descriptors.push_back(Tex[i]->SRV);
            }
            break;
        }
        case EShaderBindingType::BUFFER_SRV:
        case EShaderBindingType::BUFFER_TEXEL_SRV:
        {
            jBuffer_DX12* Buf = (jBuffer_DX12*)ShaderBinding->Resource->GetResource();
            check(Buf->Buffer);
            
            if (ShaderBinding->IsInline)
            {
                RootParameterInlines.push_back(std::make_pair(jInlineRootParamType::SRV, Buf->GetGPUAddress()));
            }
            else
            {
                Descriptors.push_back(Buf->SRV);
            }
            break;
        }
        case EShaderBindingType::TEXTURE_UAV:
        {
            jTexture_DX12* Tex = (jTexture_DX12*)ShaderBinding->Resource->GetResource();
            Descriptors.push_back(Tex->UAV);
            break;
        }
        case EShaderBindingType::BUFFER_UAV:
        case EShaderBindingType::BUFFER_UAV_DYNAMIC:
        case EShaderBindingType::BUFFER_TEXEL_UAV:
        {
            jBuffer_DX12* Buf = (jBuffer_DX12*)ShaderBinding->Resource->GetResource();
            check(Buf->Buffer);
            if (ShaderBinding->IsInline)
            {
                RootParameterInlines.push_back(std::make_pair(jInlineRootParamType::UAV, Buf->GetGPUAddress()));
            }
            else
            {
                Descriptors.push_back(Buf->UAV);
            }
            break;
        }
        case EShaderBindingType::SAMPLER:
        {
            jSamplerStateInfo_DX12* Sampler = (jSamplerStateInfo_DX12*)ShaderBinding->Resource->GetResource();
            check(Sampler);
            SamplerDescriptors.push_back(Sampler->SamplerSRV);
            break;
        }
        case EShaderBindingType::SUBPASS_INPUT_ATTACHMENT:
        case EShaderBindingType::MAX:
        default:
            check(0);
            break;
        }
    }
}

void jShaderBindingInstance_DX12::BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot /*= 0*/) const
{

}

void jShaderBindingInstance_DX12::BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot /*= 0*/) const
{

}

void* jShaderBindingInstance_DX12::GetHandle() const
{
    return ShaderBindingLayout->GetHandle();
}

void jShaderBindingInstance_DX12::BindGraphics(jCommandBuffer_DX12* InCommandList)
{
    auto CommandList = InCommandList->Get();
    
    int32 index = 0;
    for (index = 0; index < RootParameterInlines.size(); ++index)
    {
        switch(RootParameterInlines[index].first)
        {
        case jInlineRootParamType::CBV:
            CommandList->SetGraphicsRootConstantBufferView(index, RootParameterInlines[index].second);
            break;
        case jInlineRootParamType::SRV:
            CommandList->SetGraphicsRootShaderResourceView(index, RootParameterInlines[index].second);
            break;
        case jInlineRootParamType::UAV:
            CommandList->SetGraphicsRootUnorderedAccessView(index, RootParameterInlines[index].second);
        default:
            break;
        }        
    }
    CommandList->SetGraphicsRootDescriptorTable(index++, InCommandList->OnlineDescriptorHeap->GetGPUHandle());		// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap
    CommandList->SetGraphicsRootDescriptorTable(index++, InCommandList->OnlineSamplerDescriptorHeap->GetGPUHandle());	// SamplerState test
}

void jShaderBindingInstance_DX12::BindCompute(jCommandBuffer_DX12* InCommandList)
{
    auto CommandList = InCommandList->Get();

    int32 index = 0;
    for (index = 0; index < RootParameterInlines.size(); ++index)
    {
        switch (RootParameterInlines[index].first)
        {
        case jInlineRootParamType::CBV:
            CommandList->SetComputeRootConstantBufferView(index, RootParameterInlines[index].second);
            break;
        case jInlineRootParamType::SRV:
            CommandList->SetComputeRootShaderResourceView(index, RootParameterInlines[index].second);
            break;
        case jInlineRootParamType::UAV:
            CommandList->SetComputeRootUnorderedAccessView(index, RootParameterInlines[index].second);
        default:
            break;
        }
    }
    CommandList->SetComputeRootDescriptorTable(index++, InCommandList->OnlineDescriptorHeap->GetGPUHandle());		// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap
    CommandList->SetComputeRootDescriptorTable(index++, InCommandList->OnlineSamplerDescriptorHeap->GetGPUHandle());	// SamplerState test
}

void jShaderBindingInstance_DX12::CopyToOnlineDescriptorHeap(jCommandBuffer_DX12* InCommandList)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    {
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> DestDescriptor;
        DestDescriptor.resize(Descriptors.size());

        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> SrcDescriptor;
        SrcDescriptor.resize(Descriptors.size());

        for (int32 i = 0; i < Descriptors.size(); ++i)
        {
            SrcDescriptor[i] = Descriptors[i].CPUHandle;

            jDescriptor_DX12 Descriptor = InCommandList->OnlineDescriptorHeap->Alloc();
            check(Descriptor.IsValid());
            DestDescriptor[i] = Descriptor.CPUHandle;
        }

        g_rhi_dx12->Device->CopyDescriptors(DestDescriptor.size(), &DestDescriptor[0], nullptr
            , SrcDescriptor.size(), &SrcDescriptor[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    {
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> DestSamplerDescriptor;
        DestSamplerDescriptor.resize(SamplerDescriptors.size());

        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> SrcSamplerDescriptor;
        SrcSamplerDescriptor.resize(SamplerDescriptors.size());

        for (int32 i = 0; i < SamplerDescriptors.size(); ++i)
        {
            SrcSamplerDescriptor[i] = SamplerDescriptors[i].CPUHandle;

            jDescriptor_DX12 Descriptor = InCommandList->OnlineSamplerDescriptorHeap->Alloc();
            check(Descriptor.IsValid());
            DestSamplerDescriptor[i] = Descriptor.CPUHandle;
        }

        g_rhi_dx12->Device->CopyDescriptors(DestSamplerDescriptor.size(), &DestSamplerDescriptor[0], nullptr
            , SrcSamplerDescriptor.size(), &SrcSamplerDescriptor[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
}
