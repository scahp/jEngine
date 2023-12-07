#include "pch.h"
#include "jShaderBindingInstance_DX12.h"
#include "jShaderBindingLayout_DX12.h"
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

    check(ShaderBindingsLayouts);
    check(ShaderBindingsLayouts->GetShaderBindingsLayout().NumOfData == InShaderBindingArray.NumOfData);

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
            check(!UniformBuffer->IsUseRingBuffer() || (UniformBuffer->IsUseRingBuffer() && ShaderBinding->IsInline));
            if (ShaderBinding->IsInline)
            {
                RootParameterInlines.push_back({ 
                    .Type = jInlineRootParamType::CBV, .GPUVirtualAddress = UniformBuffer->GetGPUAddress(), .ResourceName = UniformBuffer->ResourceName, .Resource = UniformBuffer });
            }
            else
            {
                Descriptors.push_back({ .Descriptor = UniformBuffer->GetCBV(), .ResourceName = UniformBuffer->ResourceName, .Resource = UniformBuffer });
            }            
            break;
        }
        case EShaderBindingType::TEXTURE_SAMPLER_SRV:
        {
            const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(ShaderBinding->Resource);
            if (ensure(tbor && tbor->Texture))
            {
                jTexture_DX12* TexDX12 = (jTexture_DX12*)tbor->Texture;
                Descriptors.push_back({ .Descriptor = TexDX12->SRV, .ResourceName = TexDX12->ResourceName, .Resource = TexDX12 });

                if (tbor->SamplerState)
                {
                    jSamplerStateInfo_DX12* SamplerDX12 = (jSamplerStateInfo_DX12*)tbor->SamplerState;
                    check(SamplerDX12);
                    SamplerDescriptors.push_back({ .Descriptor = SamplerDX12->SamplerSRV, .ResourceName = SamplerDX12->ResourceName, .Resource = SamplerDX12 });
                }
                else
                {
                    // check(0);   // todo : need to set DefaultSamplerState

                    const jSamplerStateInfo_DX12* SamplerDX12 = (jSamplerStateInfo_DX12*)TSamplerStateInfo<ETextureFilter::LINEAR_MIPMAP_LINEAR, ETextureFilter::LINEAR_MIPMAP_LINEAR
                        , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, 0.0f, 16.0f>::Create();
                    check(SamplerDX12);
                    SamplerDescriptors.push_back({ .Descriptor = SamplerDX12->SamplerSRV, .ResourceName = SamplerDX12->ResourceName, .Resource = SamplerDX12 });
                }
            }
            break;
        }
        case EShaderBindingType::TEXTURE_SRV:
        {
            jTexture_DX12* Tex = (jTexture_DX12*)ShaderBinding->Resource->GetResource();
            Descriptors.push_back({ .Descriptor = Tex->SRV, .ResourceName = Tex->ResourceName, .Resource = Tex });
            break;
        }
        case EShaderBindingType::TEXTURE_ARRAY_SRV:
        {
            jTexture_DX12** Tex = (jTexture_DX12**)ShaderBinding->Resource->GetResource();
            for(int32 i=0;i< ShaderBinding->Resource->NumOfResource();++i)
            {
                check(Tex[i]);
                Descriptors.push_back({ .Descriptor = Tex[i]->SRV, .ResourceName = Tex[i]->ResourceName, .Resource = Tex[i] });
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
                RootParameterInlines.push_back({ .Type = jInlineRootParamType::SRV, .GPUVirtualAddress = Buf->GetGPUAddress(), .ResourceName = Buf->ResourceName, .Resource = Buf });
            }
            else
            {
                Descriptors.push_back({ .Descriptor = Buf->SRV, .ResourceName = Buf->ResourceName, .Resource = Buf });
            }
            break;
        }
        case EShaderBindingType::TEXTURE_UAV:
        {
            jTexture_DX12* Tex = (jTexture_DX12*)ShaderBinding->Resource->GetResource();
            const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(ShaderBinding->Resource);
            if (tbor->MipLevel == 0)
            {
                Descriptors.push_back({ .Descriptor = Tex->UAV, .ResourceName = Tex->ResourceName, .Resource = Tex });
            }
            else
            {
                auto it_find = Tex->UAVMipMap.find(tbor->MipLevel);
                if (it_find != Tex->UAVMipMap.end() && it_find->second.IsValid())
                    Descriptors.push_back({ .Descriptor = it_find->second, .ResourceName = Tex->ResourceName, .Resource = Tex });
                else
                    Descriptors.push_back({ .Descriptor = Tex->UAV, .ResourceName = Tex->ResourceName, .Resource = Tex });
            }            
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
                RootParameterInlines.push_back({ .Type = jInlineRootParamType::UAV, .GPUVirtualAddress = Buf->GetGPUAddress(), .ResourceName = Buf->ResourceName, .Resource = Buf });
            }
            else
            {
                Descriptors.push_back({ .Descriptor = Buf->UAV, .ResourceName = Buf->ResourceName, .Resource = Buf });
            }
            break;
        }
        case EShaderBindingType::SAMPLER:
        {
            jSamplerStateInfo_DX12* Sampler = (jSamplerStateInfo_DX12*)ShaderBinding->Resource->GetResource();
            check(Sampler);
            SamplerDescriptors.push_back({ .Descriptor = Sampler->SamplerSRV, .ResourceName = Sampler->ResourceName, .Resource = Sampler });
            break;
        }
        case EShaderBindingType::SUBPASS_INPUT_ATTACHMENT:
        case EShaderBindingType::MAX:
        default:
            check(0);
            break;
        }
    }

#if _DEBUG
    // validation
    for(int32 i=0;i<(int32)Descriptors.size();++i)
    {
        const jShaderBinding* ShaderBinding = InShaderBindingArray[i];
        ensure(Descriptors[i].IsValid());
    }
#endif
}

void* jShaderBindingInstance_DX12::GetHandle() const
{
    return ShaderBindingsLayouts->GetHandle();
}

void jShaderBindingInstance_DX12::BindGraphics(jCommandBuffer_DX12* InCommandList, int32& InOutStartIndex) const
{
    check(InCommandList);

    auto CommandList = InCommandList->Get();
    check(CommandList);

    int32 index = 0;
    for (index = 0; index < RootParameterInlines.size(); ++index, ++InOutStartIndex)
    {
        switch(RootParameterInlines[index].Type)
        {
        case jInlineRootParamType::CBV:
            CommandList->SetGraphicsRootConstantBufferView(InOutStartIndex, RootParameterInlines[index].GPUVirtualAddress);
            break;
        case jInlineRootParamType::SRV:
            CommandList->SetGraphicsRootShaderResourceView(InOutStartIndex, RootParameterInlines[index].GPUVirtualAddress);
            break;
        case jInlineRootParamType::UAV:
            CommandList->SetGraphicsRootUnorderedAccessView(InOutStartIndex, RootParameterInlines[index].GPUVirtualAddress);
        default:
            break;
        }        
    }
}

void jShaderBindingInstance_DX12::BindCompute(jCommandBuffer_DX12* InCommandList, int32& InOutStartIndex)
{
    check(InCommandList);

    auto CommandList = InCommandList->Get();
    check(CommandList);

    int32 index = 0;
    for (index = 0; index < RootParameterInlines.size(); ++index, ++InOutStartIndex)
    {
        switch (RootParameterInlines[index].Type)
        {
        case jInlineRootParamType::CBV:
            CommandList->SetComputeRootConstantBufferView(InOutStartIndex, RootParameterInlines[index].GPUVirtualAddress);
            break;
        case jInlineRootParamType::SRV:
            CommandList->SetComputeRootShaderResourceView(InOutStartIndex, RootParameterInlines[index].GPUVirtualAddress);
            break;
        case jInlineRootParamType::UAV:
            CommandList->SetComputeRootUnorderedAccessView(InOutStartIndex, RootParameterInlines[index].GPUVirtualAddress);
        default:
            break;
        }
    }
}

void jShaderBindingInstance_DX12::CopyToOnlineDescriptorHeap(jCommandBuffer_DX12* InCommandList)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (Descriptors.size() > 0)
    {
        check(Descriptors.size() <= 20);
        jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 20> DestDescriptor;
        jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 20> SrcDescriptor;

        for (int32 i = 0; i < Descriptors.size(); ++i)
        {
            SrcDescriptor.Add(Descriptors[i].Descriptor.CPUHandle);

            jDescriptor_DX12 Descriptor = InCommandList->OnlineDescriptorHeap->Alloc();
            check(Descriptor.IsValid());
            DestDescriptor.Add(Descriptor.CPUHandle);
        }

        g_rhi_dx12->Device->CopyDescriptors((uint32)DestDescriptor.NumOfData, &DestDescriptor[0], nullptr
            , (uint32)SrcDescriptor.NumOfData, &SrcDescriptor[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (SamplerDescriptors.size() > 0)
    {
        check(Descriptors.size() <= 20);
        jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 20> DestSamplerDescriptor;
        jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 20> SrcSamplerDescriptor;

        for (int32 i = 0; i < SamplerDescriptors.size(); ++i)
        {
            SrcSamplerDescriptor.Add(SamplerDescriptors[i].Descriptor.CPUHandle);

            jDescriptor_DX12 Descriptor = InCommandList->OnlineSamplerDescriptorHeap->Alloc();
            check(Descriptor.IsValid());
            DestSamplerDescriptor.Add(Descriptor.CPUHandle);
        }

        g_rhi_dx12->Device->CopyDescriptors((uint32)DestSamplerDescriptor.NumOfData, &DestSamplerDescriptor[0], nullptr
            , (uint32)SrcSamplerDescriptor.NumOfData, &SrcSamplerDescriptor[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
}

//ID3D12RootSignature* jShaderBindingInstance_DX12::GetRootSignature() const
//{
//    return ShaderBindingLayout->GetRootSignature();
//}
