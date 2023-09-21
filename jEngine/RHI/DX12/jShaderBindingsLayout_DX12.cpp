#include "pch.h"
#include "jShaderBindingsLayout_DX12.h"
#include "jShaderBindingInstance_DX12.h"

robin_hood::unordered_map<size_t, ComPtr<ID3D12RootSignature>> jShaderBindingsLayout_DX12::GRootSignaturePool;
jMutexRWLock jShaderBindingsLayout_DX12::GRootSignatureLock;

bool jShaderBindingsLayout_DX12::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    InShaderBindingArray.CloneWithoutResource(ShaderBindingArray);

    //int32 SRVIndex = 0;
    //int32 UAVIndex = 0;
    //int32 CBVIndex = 0;
    //int32 SamplerIndex = 0;

    //for (int32 i = 0; i < ShaderBindingArray.NumOfData; ++i)
    //{
    //    const jShaderBinding* ShaderBinding = ShaderBindingArray[i];
    //    
    //    check(!ShaderBinding->IsInline || (ShaderBinding->IsInline &&
    //        (ShaderBinding->BindingType == EShaderBindingType::UNIFORMBUFFER
    //            || ShaderBinding->BindingType == EShaderBindingType::UNIFORMBUFFER_DYNAMIC
    //            || ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV
    //            || ShaderBinding->BindingType == EShaderBindingType::BUFFER_UAV)));

    //    switch (ShaderBinding->BindingType)
    //    {
    //    case EShaderBindingType::UNIFORMBUFFER:
    //    case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
    //    {
    //        if (ShaderBinding->IsInline)
    //        {
    //            RootParameters.push_back({});
    //            D3D12_ROOT_PARAMETER1& rootParameter = RootParameters[RootParameters.size() - 1];
    //            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    //            rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    //            rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    //            rootParameter.Descriptor.ShaderRegister = CBVIndex;
    //            rootParameter.Descriptor.RegisterSpace = 0;
    //        }
    //        else
    //        {
    //            Descriptors.push_back({});
    //            D3D12_DESCRIPTOR_RANGE1& range = Descriptors[Descriptors.size() - 1];
    //            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    //            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    //            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
    //            range.BaseShaderRegister = CBVIndex;
    //            range.RegisterSpace = 0;
    //            range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
    //                ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
    //        }

    //        CBVIndex += ShaderBinding->NumOfDescriptors;
    //        break;
    //    }
    //    case EShaderBindingType::TEXTURE_SAMPLER_SRV:
    //    case EShaderBindingType::TEXTURE_SRV:
    //    case EShaderBindingType::TEXTURE_ARRAY_SRV:
    //    case EShaderBindingType::BUFFER_SRV:
    //    case EShaderBindingType::BUFFER_TEXEL_SRV:
    //    {
    //        if (ShaderBinding->IsInline && (ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV))
    //        {
    //            RootParameters.push_back({});
    //            D3D12_ROOT_PARAMETER1& rootParameter = RootParameters[RootParameters.size() - 1];
    //            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    //            rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    //            rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    //            rootParameter.Descriptor.ShaderRegister = SRVIndex;
    //            rootParameter.Descriptor.RegisterSpace = 0;
    //        }
    //        else
    //        {
    //            Descriptors.push_back({});
    //            D3D12_DESCRIPTOR_RANGE1& range = Descriptors[Descriptors.size() - 1];
    //            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    //            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    //            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
    //            range.BaseShaderRegister = SRVIndex;
    //            range.RegisterSpace = 0;
    //            range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
    //                ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
    //        }

    //        SRVIndex += ShaderBinding->NumOfDescriptors;
    //        break;
    //    }
    //    case EShaderBindingType::TEXTURE_UAV:
    //    case EShaderBindingType::BUFFER_UAV:
    //    case EShaderBindingType::BUFFER_UAV_DYNAMIC:
    //    case EShaderBindingType::BUFFER_TEXEL_UAV:
    //    {
    //        if (ShaderBinding->IsInline && (ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV))
    //        {
    //            RootParameters.push_back({});
    //            D3D12_ROOT_PARAMETER1& rootParameter = RootParameters[RootParameters.size() - 1];
    //            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    //            rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    //            rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    //            rootParameter.Descriptor.ShaderRegister = UAVIndex;
    //            rootParameter.Descriptor.RegisterSpace = 0;
    //        }
    //        else
    //        {
    //            Descriptors.push_back({});
    //            D3D12_DESCRIPTOR_RANGE1& range = Descriptors[Descriptors.size() - 1];
    //            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    //            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    //            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
    //            range.BaseShaderRegister = UAVIndex;
    //            range.RegisterSpace = 0;
    //            range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
    //                ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
    //        }

    //        ++UAVIndex;
    //        break;
    //    }
    //    case EShaderBindingType::SAMPLER:
    //    {
    //        SamplerDescriptors.push_back({});
    //        D3D12_DESCRIPTOR_RANGE1& range = SamplerDescriptors[SamplerDescriptors.size() - 1];
    //        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    //        range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    //        range.NumDescriptors = ShaderBinding->NumOfDescriptors;
    //        range.BaseShaderRegister = SamplerIndex;
    //        range.RegisterSpace = 0;
    //        range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
    //            ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;

    //        SamplerIndex += ShaderBinding->NumOfDescriptors;
    //        break;
    //    }
    //    case EShaderBindingType::SUBPASS_INPUT_ATTACHMENT:
    //    case EShaderBindingType::MAX:
    //    default:
    //        check(0);
    //        break;
    //    }
    //}

    //{
    //    RootParameters.push_back({});
    //    D3D12_ROOT_PARAMETER1& rootParameter = RootParameters[RootParameters.size() - 1];

    //    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    //    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    //    rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)Descriptors.size();
    //    rootParameter.DescriptorTable.pDescriptorRanges = &Descriptors[0];
    //}

    //{
    //    RootParameters.push_back({});
    //    D3D12_ROOT_PARAMETER1& rootParameter = RootParameters[RootParameters.size() - 1];
    //    
    //    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    //    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    //    rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)SamplerDescriptors.size();
    //    rootParameter.DescriptorTable.pDescriptorRanges = &SamplerDescriptors[0];
    //}

    //// RootSignature 생성
    //D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    //rootSignatureDesc.NumParameters = (uint32)RootParameters.size();
    //rootSignatureDesc.pParameters = &RootParameters[0];
    //rootSignatureDesc.NumStaticSamplers = 0;
    //rootSignatureDesc.pStaticSamplers = nullptr;
    //rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    //D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = { };
    //versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    //versionedDesc.Desc_1_1 = rootSignatureDesc;

    //ComPtr<ID3DBlob> signature;
    //ComPtr<ID3DBlob> error;
    //if (JFAIL_E(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error), error))
    //    return false;

    //if (JFAIL(g_rhi_dx12->Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&RootSignature))))
    //    return false;

    return true;
}

jShaderBindingInstance* jShaderBindingsLayout_DX12::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
{
    auto ShaderBindingInstance = new jShaderBindingInstance_DX12();
    ShaderBindingInstance->ShaderBindingsLayouts = this;
    ShaderBindingInstance->Initialize(InShaderBindingArray);
    ShaderBindingInstance->SetType(InType);
    
    return ShaderBindingInstance;
}

void jShaderBindingsLayout_DX12::GetRootParameters(std::vector<D3D12_ROOT_PARAMETER1>& OutRootParameters, std::vector<D3D12_DESCRIPTOR_RANGE1>& OutDescriptors
    , std::vector<D3D12_DESCRIPTOR_RANGE1>& OutSamplerDescriptors, const jShaderBindingArray& InShaderBindingArray, int32 InRegisterSpace)
{
    const uint32 OldDescriptorSize = (uint32)OutDescriptors.size();
    const uint32 OldSamplerDescriptorSize = (uint32)OutSamplerDescriptors.size();

    int32 SRVIndex = 0;
    int32 UAVIndex = 0;
    int32 CBVIndex = 0;
    int32 SamplerIndex = 0;

    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
    {
        const jShaderBinding* ShaderBinding = InShaderBindingArray[i];

        check(!ShaderBinding->IsInline || (ShaderBinding->IsInline &&
            (ShaderBinding->BindingType == EShaderBindingType::UNIFORMBUFFER
                || ShaderBinding->BindingType == EShaderBindingType::UNIFORMBUFFER_DYNAMIC
                || ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV
                || ShaderBinding->BindingType == EShaderBindingType::BUFFER_UAV)));

        switch (ShaderBinding->BindingType)
        {
        case EShaderBindingType::UNIFORMBUFFER:
        case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
        {
            if (ShaderBinding->IsInline)
            {
                OutRootParameters.push_back({});
                D3D12_ROOT_PARAMETER1& rootParameter = OutRootParameters[OutRootParameters.size() - 1];
                rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                rootParameter.Descriptor.ShaderRegister = CBVIndex;
                rootParameter.Descriptor.RegisterSpace = InRegisterSpace;
            }
            else
            {
                OutDescriptors.push_back({});
                D3D12_DESCRIPTOR_RANGE1& range = OutDescriptors[OutDescriptors.size() - 1];
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
                range.NumDescriptors = ShaderBinding->NumOfDescriptors;
                range.BaseShaderRegister = CBVIndex;
                range.RegisterSpace = InRegisterSpace;
                range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                    ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
            }

            CBVIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::TEXTURE_SAMPLER_SRV:
        case EShaderBindingType::TEXTURE_SRV:
        case EShaderBindingType::TEXTURE_ARRAY_SRV:
        case EShaderBindingType::BUFFER_SRV:
        case EShaderBindingType::BUFFER_TEXEL_SRV:
        {
            if (ShaderBinding->IsInline && (ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV))
            {
                OutRootParameters.push_back({});
                D3D12_ROOT_PARAMETER1& rootParameter = OutRootParameters[OutRootParameters.size() - 1];
                rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                rootParameter.Descriptor.ShaderRegister = SRVIndex;
                rootParameter.Descriptor.RegisterSpace = InRegisterSpace;
            }
            else
            {
                OutDescriptors.push_back({});
                D3D12_DESCRIPTOR_RANGE1& range = OutDescriptors[OutDescriptors.size() - 1];
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
                range.NumDescriptors = ShaderBinding->NumOfDescriptors;
                range.BaseShaderRegister = SRVIndex;
                range.RegisterSpace = InRegisterSpace;
                range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                    ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
            }

            SRVIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::TEXTURE_UAV:
        case EShaderBindingType::BUFFER_UAV:
        case EShaderBindingType::BUFFER_UAV_DYNAMIC:
        case EShaderBindingType::BUFFER_TEXEL_UAV:
        {
            if (ShaderBinding->IsInline && (ShaderBinding->BindingType == EShaderBindingType::BUFFER_SRV))
            {
                OutRootParameters.push_back({});
                D3D12_ROOT_PARAMETER1& rootParameter = OutRootParameters[OutRootParameters.size() - 1];
                rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
                rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                rootParameter.Descriptor.ShaderRegister = UAVIndex;
                rootParameter.Descriptor.RegisterSpace = InRegisterSpace;
            }
            else
            {
                OutDescriptors.push_back({});
                D3D12_DESCRIPTOR_RANGE1& range = OutDescriptors[OutDescriptors.size() - 1];
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
                range.NumDescriptors = ShaderBinding->NumOfDescriptors;
                range.BaseShaderRegister = UAVIndex;
                range.RegisterSpace = InRegisterSpace;
                range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                    ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;
            }

            ++UAVIndex;
            break;
        }
        case EShaderBindingType::SAMPLER:
        {
            OutSamplerDescriptors.push_back({});
            D3D12_DESCRIPTOR_RANGE1& range = OutSamplerDescriptors[OutSamplerDescriptors.size() - 1];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
            range.BaseShaderRegister = SamplerIndex;
            range.RegisterSpace = InRegisterSpace;
            range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;

            SamplerIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::SUBPASS_INPUT_ATTACHMENT:
        case EShaderBindingType::MAX:
        default:
            check(0);
            break;
        }
    }

    if ((uint32)OutDescriptors.size() > OldDescriptorSize)
    {
        OutRootParameters.push_back({});
        D3D12_ROOT_PARAMETER1& rootParameter = OutRootParameters[OutRootParameters.size() - 1];

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)OutDescriptors.size() - OldDescriptorSize;
        rootParameter.DescriptorTable.pDescriptorRanges = &OutDescriptors[OldDescriptorSize];
    }

    if ((uint32)OutSamplerDescriptors.size() > OldSamplerDescriptorSize)
    {
        OutRootParameters.push_back({});
        D3D12_ROOT_PARAMETER1& rootParameter = OutRootParameters[OutRootParameters.size() - 1];

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameter.DescriptorTable.NumDescriptorRanges = (uint32)OutSamplerDescriptors.size() - OldSamplerDescriptorSize;
        rootParameter.DescriptorTable.pDescriptorRanges = &OutSamplerDescriptors[OldSamplerDescriptorSize];
    }
}

ID3D12RootSignature* jShaderBindingsLayout_DX12::CreateRootSignature(const jShaderBindingInstanceArray& InBindingInstanceArray)
{
    if (InBindingInstanceArray.NumOfData <= 0)
        return nullptr;

    jShaderBindingsLayoutArray LayoutArray;
    for (int32 i = 0; i < InBindingInstanceArray.NumOfData; ++i)
    {
        LayoutArray.Add(InBindingInstanceArray[i]->ShaderBindingsLayouts);
    }
    return jShaderBindingsLayout_DX12::CreateRootSignature(LayoutArray);
}

ID3D12RootSignature* jShaderBindingsLayout_DX12::CreateRootSignature(const jShaderBindingsLayoutArray& InBindingLayoutArray)
{
    if (InBindingLayoutArray.NumOfData <= 0)
        return nullptr;

    size_t hash = InBindingLayoutArray.GetHash();

    {
        jScopeReadLock sr(&GRootSignatureLock);
        auto it_find = GRootSignaturePool.find(hash);
        if (GRootSignaturePool.end() != it_find)
        {
            return it_find->second.Get();
        }
    }

    {
        jScopeWriteLock sw(&GRootSignatureLock);

        // Try again, to avoid entering creation section simultaneously.
        auto it_find = GRootSignaturePool.find(hash);
        if (GRootSignaturePool.end() != it_find)
        {
            return it_find->second.Get();
        }

        std::vector<D3D12_ROOT_PARAMETER1> RootParameters;
        std::vector<D3D12_DESCRIPTOR_RANGE1> Descriptors;
        std::vector<D3D12_DESCRIPTOR_RANGE1> SamplerDescriptors;
        Descriptors.reserve(100);
        SamplerDescriptors.reserve(100);

        for (int32 i = 0; i < InBindingLayoutArray.NumOfData; ++i)
        {
            jShaderBindingsLayout_DX12* Layout = (jShaderBindingsLayout_DX12*)InBindingLayoutArray[i];
            check(Layout);

            jShaderBindingsLayout_DX12::GetRootParameters(RootParameters, Descriptors, SamplerDescriptors, Layout->ShaderBindingArray, i);
        }

        // RootSignature 생성
        D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
        rootSignatureDesc.NumParameters = (uint32)RootParameters.size();
        rootSignatureDesc.pParameters = &RootParameters[0];
        rootSignatureDesc.NumStaticSamplers = 0;
        rootSignatureDesc.pStaticSamplers = nullptr;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = { };
        versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        versionedDesc.Desc_1_1 = rootSignatureDesc;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        if (JFAIL_E(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error), error))
            return nullptr;

        ComPtr<ID3D12RootSignature> RootSignature;
        if (JFAIL(g_rhi_dx12->Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&RootSignature))))
            return nullptr;

        GRootSignaturePool[hash] = RootSignature;
        return RootSignature.Get();
    }
}

