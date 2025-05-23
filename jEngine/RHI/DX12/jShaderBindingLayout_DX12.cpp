﻿#include "pch.h"
#include "jShaderBindingLayout_DX12.h"
#include "jShaderBindingInstance_DX12.h"

robin_hood::unordered_map<size_t, ComPtr<ID3D12RootSignature>> jShaderBindingLayout_DX12::GRootSignaturePool;
jMutexRWLock jShaderBindingLayout_DX12::GRootSignatureLock;

// Below option will be work like switch
#define FORCE_USE_DESCRIPTOR_OFFSET_BY_USING_AUTO_CALCULATION_FOR_BINDLESS 1
#define FORCE_USE_D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND (1 && !FORCE_USE_DESCRIPTOR_OFFSET_BY_USING_AUTO_CALCULATION_FOR_BINDLESS)

void jRootParameterExtractor::Extract(int32& InOutDescriptorOffset, int32& InOutSamplerDescriptorOffset, const jShaderBindingArray& InShaderBindingArray, int32 InRegisterSpace)
{
    // InRootParameters 의 맨 앞쪽에 inline descriptor 를 무조건 먼저 배치함.
    int32 BindingIndex = 0;     // // To support both API(Vulkan, DX12) : Vulkna require a unique bindingindex.

    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
    {
        const jShaderBinding* ShaderBinding = InShaderBindingArray[i];
        const bool IsBindless = ShaderBinding->IsBindless;

        check(!!(ShaderBinding->AccessStageFlags & ShaderAccessStageFlags));

        check(!ShaderBinding->IsInline || (ShaderBinding->IsInline &&
            (ShaderBinding->BindingType == EShaderBindingType::UNIFORMBUFFER
                || ShaderBinding->BindingType == EShaderBindingType::UNIFORMBUFFER_DYNAMIC
                || ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV
                || ShaderBinding->BindingType == EShaderBindingType::ACCELERATION_STRUCTURE_SRV
                || ShaderBinding->BindingType == EShaderBindingType::BUFFER_UAV)));

        switch (ShaderBinding->BindingType)
        {
        case EShaderBindingType::UNIFORMBUFFER:
        case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
        {
            if (ShaderBinding->IsInline)
            {
                D3D12_ROOT_PARAMETER1 rootParameter = {};
                rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                rootParameter.Descriptor.ShaderRegister = BindingIndex;
                rootParameter.Descriptor.RegisterSpace = InRegisterSpace;
                RootParameters.emplace_back(rootParameter);
            }
            else
            {
                D3D12_DESCRIPTOR_RANGE1 range = {};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                range.NumDescriptors = ShaderBinding->NumOfDescriptors;
                range.BaseShaderRegister = BindingIndex;
                range.RegisterSpace = InRegisterSpace;
                if (IsBindless)
                {
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
                    range.OffsetInDescriptorsFromTableStart = InOutDescriptorOffset;
                }
                else
                {
#if FORCE_USE_DESCRIPTOR_OFFSET_BY_USING_AUTO_CALCULATION_FOR_BINDLESS
                    range.OffsetInDescriptorsFromTableStart = InOutDescriptorOffset;
#elif FORCE_USE_D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
#else
                    range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                        ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
#endif
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
                }
                InOutDescriptorOffset += ShaderBinding->NumOfDescriptors;
                Descriptors.emplace_back(range);
            }

            BindingIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::TEXTURE_SAMPLER_SRV:
        case EShaderBindingType::TEXTURE_SRV:
        case EShaderBindingType::TEXTURE_ARRAY_SRV:
        case EShaderBindingType::BUFFER_SRV:
        case EShaderBindingType::BUFFER_TEXEL_SRV:
        case EShaderBindingType::ACCELERATION_STRUCTURE_SRV:
        {
            if (ShaderBinding->IsInline && (ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV || ShaderBinding->BindingType == EShaderBindingType::ACCELERATION_STRUCTURE_SRV))
            {
                D3D12_ROOT_PARAMETER1 rootParameter = {};
                rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
                rootParameter.Descriptor.ShaderRegister = BindingIndex;
                rootParameter.Descriptor.RegisterSpace = InRegisterSpace;
                RootParameters.emplace_back(rootParameter);
            }
            else
            {
                D3D12_DESCRIPTOR_RANGE1 range = {};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                range.NumDescriptors = ShaderBinding->NumOfDescriptors;
                range.BaseShaderRegister = BindingIndex;
                range.RegisterSpace = InRegisterSpace;
                if (IsBindless)
                {
                    range.OffsetInDescriptorsFromTableStart = InOutDescriptorOffset;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
                }
                else
                {
#if FORCE_USE_DESCRIPTOR_OFFSET_BY_USING_AUTO_CALCULATION_FOR_BINDLESS
                    range.OffsetInDescriptorsFromTableStart = InOutDescriptorOffset;
#elif FORCE_USE_D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
#else
                    range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                        ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
#endif
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
                }
                InOutDescriptorOffset += ShaderBinding->NumOfDescriptors;
                Descriptors.emplace_back(range);
            }

            // todo : Texture Sampler SRV 는 Texture 와 Sampler 를 동시에 정의하기 때문에 여기에 Sampler 를 추가. Vulkan 문법이라 사용하지 않는 것도 고민해봐야 함.
            if (ShaderBinding->BindingType == EShaderBindingType::TEXTURE_SAMPLER_SRV)
            {
                D3D12_DESCRIPTOR_RANGE1 range = {};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                range.NumDescriptors = ShaderBinding->NumOfDescriptors;
                range.BaseShaderRegister = BindingIndex;
                range.RegisterSpace = InRegisterSpace;
                if (IsBindless)
                {
                    range.OffsetInDescriptorsFromTableStart = InOutSamplerDescriptorOffset;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
                }
                else
                {
#if FORCE_USE_DESCRIPTOR_OFFSET_BY_USING_AUTO_CALCULATION_FOR_BINDLESS
                    range.OffsetInDescriptorsFromTableStart = InOutSamplerDescriptorOffset;
#elif FORCE_USE_D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
#else
                    range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                        ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
#endif
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
                }
                InOutSamplerDescriptorOffset += ShaderBinding->NumOfDescriptors;
                SamplerDescriptors.emplace_back(range);

                //SamplerIndex += ShaderBinding->NumOfDescriptors;
            }

            BindingIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::TEXTURE_UAV:
        case EShaderBindingType::BUFFER_UAV:
        case EShaderBindingType::BUFFER_UAV_DYNAMIC:
        case EShaderBindingType::BUFFER_TEXEL_UAV:
        {
            if (ShaderBinding->IsInline)
            {
                D3D12_ROOT_PARAMETER1 rootParameter = {};
                rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
                rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
                rootParameter.Descriptor.ShaderRegister = BindingIndex;
                rootParameter.Descriptor.RegisterSpace = InRegisterSpace;
                RootParameters.emplace_back(rootParameter);
            }
            else
            {
                D3D12_DESCRIPTOR_RANGE1 range = {};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                range.NumDescriptors = ShaderBinding->NumOfDescriptors;
                range.BaseShaderRegister = BindingIndex;
                range.RegisterSpace = InRegisterSpace;
                if (IsBindless)
                {
                    range.OffsetInDescriptorsFromTableStart = InOutDescriptorOffset;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
                }
                else
                {
#if FORCE_USE_DESCRIPTOR_OFFSET_BY_USING_AUTO_CALCULATION_FOR_BINDLESS
                    range.OffsetInDescriptorsFromTableStart = InOutDescriptorOffset;
#elif FORCE_USE_D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
#else
                    range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                        ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
#endif
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
                }
                InOutDescriptorOffset += ShaderBinding->NumOfDescriptors;
                Descriptors.emplace_back(range);
            }

            BindingIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::SAMPLER:
        {
            D3D12_DESCRIPTOR_RANGE1 range = {};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
            range.BaseShaderRegister = BindingIndex;
            range.RegisterSpace = InRegisterSpace;
            if (IsBindless)
            {
                range.OffsetInDescriptorsFromTableStart = InOutSamplerDescriptorOffset;
                range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            }
            else
            {
#if FORCE_USE_DESCRIPTOR_OFFSET_BY_USING_AUTO_CALCULATION_FOR_BINDLESS
                range.OffsetInDescriptorsFromTableStart = InOutSamplerDescriptorOffset;
#elif FORCE_USE_D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
#else
                range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                    ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
#endif
                range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
            }
            InOutSamplerDescriptorOffset += ShaderBinding->NumOfDescriptors;
            SamplerDescriptors.emplace_back(range);

            BindingIndex += ShaderBinding->NumOfDescriptors;
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

void jRootParameterExtractor::Extract(const jShaderBindingLayoutArray& InBindingLayoutArray, int32 InRegisterSpace /*= 0*/)
{
    int32 InOutDescriptorOffset = 0;
    int32 InOutSamplerDescriptorOffset = 0;
    for (int32 i = 0; i < InBindingLayoutArray.NumOfData; ++i)
    {
        jShaderBindingLayout_DX12* Layout = (jShaderBindingLayout_DX12*)InBindingLayoutArray[i];
        check(Layout);

        Extract(InOutDescriptorOffset, InOutSamplerDescriptorOffset, Layout->GetShaderBindingsLayout(), i);
    }

    NumOfInlineRootParameter = (int32)RootParameters.size();

    if (Descriptors.size() > 0)
    {
        D3D12_ROOT_PARAMETER1 rootParameter = {};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)Descriptors.size();
        rootParameter.DescriptorTable.pDescriptorRanges = &Descriptors[0];
        RootParameters.emplace_back(rootParameter);
    }


    if (SamplerDescriptors.size() > 0)
    {
        D3D12_ROOT_PARAMETER1 rootParameter = {};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)SamplerDescriptors.size();
        rootParameter.DescriptorTable.pDescriptorRanges = &SamplerDescriptors[0];
        RootParameters.emplace_back(rootParameter);
    }
}

void jRootParameterExtractor::Extract(const jShaderBindingInstanceArray& InBindingInstanceArray, int32 InRegisterSpace /*= 0*/)
{
    int32 InOutDescriptorOffset = 0;
    int32 InOutSamplerDescriptorOffset = 0;
    for (int32 i = 0; i < InBindingInstanceArray.NumOfData; ++i)
    {
        jShaderBindingInstance_DX12* Instance = (jShaderBindingInstance_DX12*)InBindingInstanceArray[i];
        check(Instance);
        check(Instance->ShaderBindingsLayouts);

        jShaderBindingLayout_DX12* Layout = (jShaderBindingLayout_DX12*)Instance->ShaderBindingsLayouts;
        check(Layout);
        Extract(InOutDescriptorOffset, InOutSamplerDescriptorOffset, Layout->GetShaderBindingsLayout(), i);
    }

    NumOfInlineRootParameter = (int32)RootParameters.size();

    if (Descriptors.size() > 0)
    {
        D3D12_ROOT_PARAMETER1 rootParameter = {};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)Descriptors.size();
        rootParameter.DescriptorTable.pDescriptorRanges = &Descriptors[0];
        RootParameters.emplace_back(rootParameter);
    }


    if (SamplerDescriptors.size() > 0)
    {
        D3D12_ROOT_PARAMETER1 rootParameter = {};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)SamplerDescriptors.size();
        rootParameter.DescriptorTable.pDescriptorRanges = &SamplerDescriptors[0];
        RootParameters.emplace_back(rootParameter);
    }
}

bool jShaderBindingLayout_DX12::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    InShaderBindingArray.CloneWithoutResource(ShaderBindingArray);

    return true;
}

std::shared_ptr<jShaderBindingInstance> jShaderBindingLayout_DX12::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
{
    auto ShaderBindingInstance_DX12 = std::make_shared<jShaderBindingInstance_DX12>();
    ShaderBindingInstance_DX12->ShaderBindingsLayouts = this;
    ShaderBindingInstance_DX12->Initialize(InShaderBindingArray);
    ShaderBindingInstance_DX12->SetType(InType);
    return ShaderBindingInstance_DX12;
}

ID3D12RootSignature* jShaderBindingLayout_DX12::CreateRootSignatureInternal(size_t InHash, FuncGetRootParameterExtractor InFunc, EShaderAccessStageFlag InShaderAccessStageFlag)
{
    {
        jScopeReadLock sr(&GRootSignatureLock);
        auto it_find = GRootSignaturePool.find(InHash);
        if (GRootSignaturePool.end() != it_find)
        {
            return it_find->second.Get();
        }
    }

    {
        jScopeWriteLock sw(&GRootSignatureLock);

        // Try again, to avoid entering creation section simultaneously.
        auto it_find = GRootSignaturePool.find(InHash);
        if (GRootSignaturePool.end() != it_find)
        {
            return it_find->second.Get();
        }

        jRootParameterExtractor DescriptorExtractor(InShaderAccessStageFlag);
        InFunc(DescriptorExtractor);

        // RootSignature 생성
        D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
        rootSignatureDesc.NumParameters = (uint32)DescriptorExtractor.RootParameters.size();
        rootSignatureDesc.pParameters = &DescriptorExtractor.RootParameters[0];
        rootSignatureDesc.NumStaticSamplers = 0;
        rootSignatureDesc.pStaticSamplers = nullptr;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;        // Support for BindlessResource

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = { };
        versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        versionedDesc.Desc_1_1 = rootSignatureDesc;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        if (JFAIL_E(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error), error))
        {
            return nullptr;
        }

        ComPtr<ID3D12RootSignature> RootSignature;
        if (JFAIL(g_rhi_dx12->Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&RootSignature))))
            return nullptr;

        GRootSignaturePool[InHash] = RootSignature;
        return RootSignature.Get();
    }
}

ID3D12RootSignature* jShaderBindingLayout_DX12::CreateRootSignature(const jShaderBindingInstanceArray& InBindingInstanceArray, EShaderAccessStageFlag InShaderAccessStageFlag)
{
    if (InBindingInstanceArray.NumOfData <= 0)
        return nullptr;

    size_t hash = 0;
    for (int32 i = 0; i < InBindingInstanceArray.NumOfData; ++i)
    {
        jShaderBindingLayoutArray::GetHash(hash, i, InBindingInstanceArray[i]->ShaderBindingsLayouts);
    }

    return CreateRootSignatureInternal(hash, [&InBindingInstanceArray](jRootParameterExtractor& OutRootParameterExtractor)
        {
            OutRootParameterExtractor.Extract(InBindingInstanceArray);
        }, InShaderAccessStageFlag);
}

ID3D12RootSignature* jShaderBindingLayout_DX12::CreateRootSignature(const jShaderBindingLayoutArray& InBindingLayoutArray, EShaderAccessStageFlag InShaderAccessStageFlag)
{
    if (InBindingLayoutArray.NumOfData <= 0)
        return nullptr;

    const size_t hash = InBindingLayoutArray.GetHash();

    return CreateRootSignatureInternal(hash, [&InBindingLayoutArray](jRootParameterExtractor& OutRootParameterExtractor)
        {
            OutRootParameterExtractor.Extract(InBindingLayoutArray);
        }, InShaderAccessStageFlag);
}