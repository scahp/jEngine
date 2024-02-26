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
    DesriptorsOnlyCPU.clear();
    SamplerDescriptorsOnlyCPU.clear();

    // todo : pre calculate size
    Descriptors.reserve(200);
    RootParameterInlines.reserve(200);
    SamplerDescriptors.reserve(200);
	DesriptorsOnlyCPU.reserve(200);
	SamplerDescriptorsOnlyCPU.reserve(200);

    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
    {
        const jShaderBinding* ShaderBinding = InShaderBindingArray[i];
        check(ShaderBinding);
        check(ShaderBinding->Resource);
        check(ShaderBinding->IsBindless == ShaderBinding->Resource->IsBindless());

        const bool IsBindless = ShaderBinding->IsBindless;
        check((IsBindless && !ShaderBinding->IsInline) || !IsBindless);     // Bindless must note be inline

        switch (ShaderBinding->BindingType)
        {
        case EShaderBindingType::UNIFORMBUFFER:
        case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
        {
            if (IsBindless)
            {
                auto UniformResourceBindless = (jUniformBufferResourceBindless*)ShaderBinding->Resource;
                check(UniformResourceBindless->UniformBuffers);
                for(const auto& Resource : *UniformResourceBindless->UniformBuffers)
                {
                    check(Resource);

                    jUniformBufferBlock_DX12* UniformBuffer = (jUniformBufferBlock_DX12*)Resource;
                    Descriptors.push_back({ .Descriptor = UniformBuffer->GetCBV(), .ResourceName = UniformBuffer->ResourceName, .Resource = UniformBuffer });
                    DesriptorsOnlyCPU.push_back(UniformBuffer->GetCBV().CPUHandle);
                }
            }
            else
            {
                jUniformBufferBlock_DX12* UniformBuffer = (jUniformBufferBlock_DX12*)ShaderBinding->Resource->GetResource();
                check(UniformBuffer->GetLowLevelResource());
                //check(!UniformBuffer->IsUseRingBuffer() || (UniformBuffer->IsUseRingBuffer() && ShaderBinding->IsInline));
                if (ShaderBinding->IsInline)
                {
                    RootParameterInlines.push_back({
                        .Type = jInlineRootParamType::CBV, .GPUVirtualAddress = UniformBuffer->GetGPUAddress(), .ResourceName = UniformBuffer->ResourceName, .Resource = UniformBuffer });
                }
                else
                {
                    Descriptors.push_back({ .Descriptor = UniformBuffer->GetCBV(), .ResourceName = UniformBuffer->ResourceName, .Resource = UniformBuffer });
                    DesriptorsOnlyCPU.push_back(UniformBuffer->GetCBV().CPUHandle);
                }
            }
            break;
        }
        case EShaderBindingType::TEXTURE_SAMPLER_SRV:
        {
            if (IsBindless)
            {
                auto TextureResourceResourceBindless = (jTextureResourceBindless*)ShaderBinding->Resource;
                check(TextureResourceResourceBindless->TextureBindDatas);
                for(const auto& Resource : *TextureResourceResourceBindless->TextureBindDatas)
                {
                    check(Resource.Texture);

                    jTexture_DX12* TexDX12 = (jTexture_DX12*)Resource.Texture;
                    Descriptors.push_back({ .Descriptor = TexDX12->SRV, .ResourceName = TexDX12->ResourceName, .Resource = TexDX12 });
                    DesriptorsOnlyCPU.push_back(TexDX12->SRV.CPUHandle);

                    if (Resource.SamplerState)
                    {
                        jSamplerStateInfo_DX12* SamplerDX12 = (jSamplerStateInfo_DX12*)Resource.SamplerState;
                        check(SamplerDX12);
                        SamplerDescriptors.push_back({ .Descriptor = SamplerDX12->SamplerSRV, .ResourceName = SamplerDX12->ResourceName, .Resource = SamplerDX12 });
                        SamplerDescriptorsOnlyCPU.push_back(SamplerDX12->SamplerSRV.CPUHandle);
                    }
                    else
                    {
                        // check(0);   // todo : need to set DefaultSamplerState
                        const jSamplerStateInfo_DX12* SamplerDX12 = (jSamplerStateInfo_DX12*)TSamplerStateInfo<ETextureFilter::LINEAR_MIPMAP_LINEAR, ETextureFilter::LINEAR_MIPMAP_LINEAR
                            , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, 0.0f, 1.0f>::Create();
                        check(SamplerDX12);
                        SamplerDescriptors.push_back({ .Descriptor = SamplerDX12->SamplerSRV, .ResourceName = SamplerDX12->ResourceName, .Resource = SamplerDX12 });
                        SamplerDescriptorsOnlyCPU.push_back(SamplerDX12->SamplerSRV.CPUHandle);
                    }
                }
            }
            else
            {
                const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(ShaderBinding->Resource);
                if (ensure(tbor && tbor->Texture))
                {
                    jTexture_DX12* TexDX12 = (jTexture_DX12*)tbor->Texture;
                    Descriptors.push_back({ .Descriptor = TexDX12->SRV, .ResourceName = TexDX12->ResourceName, .Resource = TexDX12 });
                    DesriptorsOnlyCPU.push_back(TexDX12->SRV.CPUHandle);

                    if (tbor->SamplerState)
                    {
                        jSamplerStateInfo_DX12* SamplerDX12 = (jSamplerStateInfo_DX12*)tbor->SamplerState;
                        check(SamplerDX12);
                        SamplerDescriptors.push_back({ .Descriptor = SamplerDX12->SamplerSRV, .ResourceName = SamplerDX12->ResourceName, .Resource = SamplerDX12 });
                        SamplerDescriptorsOnlyCPU.push_back(SamplerDX12->SamplerSRV.CPUHandle);
                    }
                    else
                    {
                        // check(0);   // todo : need to set DefaultSamplerState

                        const jSamplerStateInfo_DX12* SamplerDX12 = (jSamplerStateInfo_DX12*)TSamplerStateInfo<ETextureFilter::LINEAR_MIPMAP_LINEAR, ETextureFilter::LINEAR_MIPMAP_LINEAR
                            , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, 0.0f, 1.0f>::Create();
                        check(SamplerDX12);
                        SamplerDescriptors.push_back({ .Descriptor = SamplerDX12->SamplerSRV, .ResourceName = SamplerDX12->ResourceName, .Resource = SamplerDX12 });
                        SamplerDescriptorsOnlyCPU.push_back(SamplerDX12->SamplerSRV.CPUHandle);
                    }
                }
            }
            break;
        }
        case EShaderBindingType::TEXTURE_SRV:
        {
            if (IsBindless)
            {
                auto TextureResourceResourceBindless = (jTextureResourceBindless*)ShaderBinding->Resource;
                check(TextureResourceResourceBindless->TextureBindDatas);
                for (const auto& Resource : *TextureResourceResourceBindless->TextureBindDatas)
                {
                    jTexture_DX12* Tex = (jTexture_DX12*)Resource.Texture;
                    Descriptors.push_back({ .Descriptor = Tex->SRV, .ResourceName = Tex->ResourceName, .Resource = Tex });
                    DesriptorsOnlyCPU.push_back(Tex->SRV.CPUHandle);
                }
            }
            else
            {
                jTexture_DX12* Tex = (jTexture_DX12*)ShaderBinding->Resource->GetResource();
                Descriptors.push_back({ .Descriptor = Tex->SRV, .ResourceName = Tex->ResourceName, .Resource = Tex });
                DesriptorsOnlyCPU.push_back(Tex->SRV.CPUHandle);
            }
            break;
        }
        case EShaderBindingType::TEXTURE_ARRAY_SRV:
        {
            if (IsBindless)
            {
                auto TextureResourceResourceArrayBindless = (jTextureArrayResourceBindless*)ShaderBinding->Resource;
                check(TextureResourceResourceArrayBindless->TextureArrayBindDatas);
                for (const auto& Resource : *TextureResourceResourceArrayBindless->TextureArrayBindDatas)
                {
                    jTexture_DX12** TexArray = (jTexture_DX12**)Resource.TextureArray;
                    for (int32 i = 0; i < Resource.InNumOfTexure; ++i)
                    {
                        check(TexArray[i]);
                        Descriptors.push_back({ .Descriptor = TexArray[i]->SRV, .ResourceName = TexArray[i]->ResourceName, .Resource = TexArray[i] });
                        DesriptorsOnlyCPU.push_back(TexArray[i]->SRV.CPUHandle);
                    }
                }
            }
            else
            {
                jTexture_DX12** Tex = (jTexture_DX12**)ShaderBinding->Resource->GetResource();
                for (int32 i = 0; i < ShaderBinding->Resource->NumOfResource(); ++i)
                {
                    check(Tex[i]);
                    Descriptors.push_back({ .Descriptor = Tex[i]->SRV, .ResourceName = Tex[i]->ResourceName, .Resource = Tex[i] });
                    DesriptorsOnlyCPU.push_back(Tex[i]->SRV.CPUHandle);
                }
            }
            break;
        }
        case EShaderBindingType::BUFFER_SRV:
        case EShaderBindingType::BUFFER_TEXEL_SRV:
        case EShaderBindingType::ACCELERATION_STRUCTURE_SRV:
        {
            if (IsBindless)
            {
                auto BufferResourceBindless = (jBufferResourceBindless*)ShaderBinding->Resource;
                check(BufferResourceBindless->Buffers);
                for (const auto& Resource : *BufferResourceBindless->Buffers)
                {
                    jBuffer_DX12* Buf = (jBuffer_DX12*)Resource;
                    Descriptors.push_back({ .Descriptor = Buf->SRV, .ResourceName = Buf->ResourceName, .Resource = Buf });
                    DesriptorsOnlyCPU.push_back(Buf->SRV.CPUHandle);
                }
            }
            else
            {
                jBuffer_DX12* Buf = (jBuffer_DX12*)ShaderBinding->Resource->GetResource();
                check(Buf->Buffer->Resource);

                if (ShaderBinding->IsInline)
                {
                    RootParameterInlines.push_back({ .Type = jInlineRootParamType::SRV, .GPUVirtualAddress = Buf->GetGPUAddress(), .ResourceName = Buf->ResourceName, .Resource = Buf });
                }
                else
                {
                    Descriptors.push_back({ .Descriptor = Buf->SRV, .ResourceName = Buf->ResourceName, .Resource = Buf });
                    DesriptorsOnlyCPU.push_back(Buf->SRV.CPUHandle);
                }
            }
            break;
        }
        case EShaderBindingType::TEXTURE_UAV:
        {
            if (IsBindless)
            {
                auto TextureResourceResourceBindless = (jTextureResourceBindless*)ShaderBinding->Resource;
                check(TextureResourceResourceBindless->TextureBindDatas);
                for (const auto& Resource : *TextureResourceResourceBindless->TextureBindDatas)
                {
                    jTexture_DX12* Tex = (jTexture_DX12*)Resource.Texture;
                    check(Tex->UAV.IsValid());
                    if (Resource.MipLevel == 0)
                    {
                        Descriptors.push_back({ .Descriptor = Tex->UAV, .ResourceName = Tex->ResourceName, .Resource = Tex });
                        DesriptorsOnlyCPU.push_back(Tex->UAV.CPUHandle);
                    }
                    else
                    {
                        auto it_find = Tex->UAVMipMap.find(Resource.MipLevel);
                        if (it_find != Tex->UAVMipMap.end() && it_find->second.IsValid())
                            Descriptors.push_back({ .Descriptor = it_find->second, .ResourceName = Tex->ResourceName, .Resource = Tex });
                        else
                            Descriptors.push_back({ .Descriptor = Tex->UAV, .ResourceName = Tex->ResourceName, .Resource = Tex });

                        DesriptorsOnlyCPU.push_back(Descriptors[Descriptors.size() - 1].Descriptor.CPUHandle);
                    }
                }
            }
            else
            {
                jTexture_DX12* Tex = (jTexture_DX12*)ShaderBinding->Resource->GetResource();
                const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(ShaderBinding->Resource);
                check(Tex->UAV.IsValid());
                if (tbor->MipLevel == 0)
                {
                    Descriptors.push_back({ .Descriptor = Tex->UAV, .ResourceName = Tex->ResourceName, .Resource = Tex });
                    DesriptorsOnlyCPU.push_back(Tex->UAV.CPUHandle);
                }
                else
                {
                    auto it_find = Tex->UAVMipMap.find(tbor->MipLevel);
                    if (it_find != Tex->UAVMipMap.end() && it_find->second.IsValid())
                        Descriptors.push_back({ .Descriptor = it_find->second, .ResourceName = Tex->ResourceName, .Resource = Tex });
                    else
                        Descriptors.push_back({ .Descriptor = Tex->UAV, .ResourceName = Tex->ResourceName, .Resource = Tex });
                    
                    DesriptorsOnlyCPU.push_back(Descriptors[Descriptors.size() - 1].Descriptor.CPUHandle);
                }
            }
            break;
        }
        case EShaderBindingType::BUFFER_UAV:
        case EShaderBindingType::BUFFER_UAV_DYNAMIC:
        case EShaderBindingType::BUFFER_TEXEL_UAV:
        {
            if (IsBindless)
            {
                auto BufferResourceBindless = (jBufferResourceBindless*)ShaderBinding->Resource;
                check(BufferResourceBindless->Buffers);
                for (const auto& Resource : *BufferResourceBindless->Buffers)
                {
                    jBuffer_DX12* Buf = (jBuffer_DX12*)Resource;
                    Descriptors.push_back({ .Descriptor = Buf->UAV, .ResourceName = Buf->ResourceName, .Resource = Buf });
                    DesriptorsOnlyCPU.push_back(Buf->UAV.CPUHandle);
                }
            }
            else
            {
                jBuffer_DX12* Buf = (jBuffer_DX12*)ShaderBinding->Resource->GetResource();
                check(Buf->Buffer->Resource);
                if (ShaderBinding->IsInline)
                {
                    RootParameterInlines.push_back({ .Type = jInlineRootParamType::UAV, .GPUVirtualAddress = Buf->GetGPUAddress(), .ResourceName = Buf->ResourceName, .Resource = Buf });
                }
                else
                {
					check(Buf->UAV.IsValid());
                    Descriptors.push_back({ .Descriptor = Buf->UAV, .ResourceName = Buf->ResourceName, .Resource = Buf });
                    DesriptorsOnlyCPU.push_back(Buf->UAV.CPUHandle);
                }
            }
            break;
        }
        case EShaderBindingType::SAMPLER:
        {
            if (IsBindless)
            {
                auto SamplerResourceBindless = (jSamplerResourceBindless*)ShaderBinding->Resource;
                check(SamplerResourceBindless->SamplerStates);
                for(const auto& Resource : *SamplerResourceBindless->SamplerStates)
                {
                    jSamplerStateInfo_DX12* Sampler = (jSamplerStateInfo_DX12*)Resource;
                    check(Sampler);
                    SamplerDescriptors.push_back({ .Descriptor = Sampler->SamplerSRV, .ResourceName = Sampler->ResourceName, .Resource = Sampler });
                    SamplerDescriptorsOnlyCPU.push_back(Sampler->SamplerSRV.CPUHandle);
                }
            }
            else
            {
                jSamplerStateInfo_DX12* Sampler = (jSamplerStateInfo_DX12*)ShaderBinding->Resource->GetResource();
                check(Sampler);
                SamplerDescriptors.push_back({ .Descriptor = Sampler->SamplerSRV, .ResourceName = Sampler->ResourceName, .Resource = Sampler });
                SamplerDescriptorsOnlyCPU.push_back(Sampler->SamplerSRV.CPUHandle);
            }
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
        ensure(Descriptors[i].IsValid());
    }
#endif
}

void* jShaderBindingInstance_DX12::GetHandle() const
{
    return ShaderBindingsLayouts->GetHandle();
}

void jShaderBindingInstance_DX12::Free()
{
    if (GetType() == jShaderBindingInstanceType::MultiFrame)
    {
        jScopedLock s(&g_rhi_dx12->MultiFrameShaderBindingInstanceLock);
        g_rhi_dx12->DeallocatorMultiFrameShaderBindingInstance.Free(shared_from_this());
    }
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
        check(Descriptors.size() <= 1000);
        jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 1000> DestDescriptor;
        InCommandList->OnlineDescriptorHeap->AllocToResourceContainer(DestDescriptor, (int32)Descriptors.size());

        g_rhi_dx12->Device->CopyDescriptors((uint32)DestDescriptor.NumOfData, &DestDescriptor[0], nullptr
            , (uint32)DesriptorsOnlyCPU.size(), &DesriptorsOnlyCPU[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (SamplerDescriptors.size() > 0)
    {
        check(Descriptors.size() <= 200);
        jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 200> DestSamplerDescriptor;
        InCommandList->OnlineSamplerDescriptorHeap->AllocToResourceContainer(DestSamplerDescriptor, (int32)SamplerDescriptors.size());

        g_rhi_dx12->Device->CopyDescriptors((uint32)DestSamplerDescriptor.NumOfData, &DestSamplerDescriptor[0], nullptr
            , (uint32)SamplerDescriptorsOnlyCPU.size(), &SamplerDescriptorsOnlyCPU[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
}
