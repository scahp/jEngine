#include "pch.h"
#include "jShaderBindingsLayout_DX12.h"
#include "jShaderBindingInstance_DX12.h"

bool jShaderBindingsLayout_DX12::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    int32 SRVIndex = 0;
    int32 UAVIndex = 0;
    int32 CBVIndex = 0;
    int32 SamplerIndex = 0;

    std::vector<D3D12_DESCRIPTOR_RANGE1> Descriptors;
    std::vector<D3D12_DESCRIPTOR_RANGE1> SamplerDescriptors;

    InShaderBindingArray.CloneWithoutResource(ShaderBindingArray);
    for (int32 i = 0; i < ShaderBindingArray.NumOfData; ++i)
    {
        const jShaderBinding* ShaderBinding = ShaderBindingArray[i];

        switch (ShaderBinding->BindingType)
        {
        case EShaderBindingType::UNIFORMBUFFER:
        case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
        {
            Descriptors.push_back({});
            D3D12_DESCRIPTOR_RANGE1& range = Descriptors[Descriptors.size() - 1];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
            range.BaseShaderRegister = CBVIndex;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;

            CBVIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::TEXTURE_SAMPLER_SRV:
        case EShaderBindingType::TEXTURE_SRV:
        case EShaderBindingType::TEXTURE_ARRAY_SRV:
        case EShaderBindingType::BUFFER_SRV:
        case EShaderBindingType::BUFFER_TEXEL_SRV:
        {
            Descriptors.push_back({});
            D3D12_DESCRIPTOR_RANGE1& range = Descriptors[Descriptors.size() - 1];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
            range.BaseShaderRegister = SRVIndex;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;

            SRVIndex += ShaderBinding->NumOfDescriptors;
            break;
        }
        case EShaderBindingType::TEXTURE_UAV:
        case EShaderBindingType::BUFFER_UAV:
        case EShaderBindingType::BUFFER_UAV_DYNAMIC:
        case EShaderBindingType::BUFFER_TEXEL_UAV:
        {
            Descriptors.push_back({});
            D3D12_DESCRIPTOR_RANGE1& range = Descriptors[Descriptors.size() - 1];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
            range.BaseShaderRegister = UAVIndex;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = (ShaderBinding->BindingPoint == -1)
                ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND : ShaderBinding->BindingPoint;

            ++UAVIndex;
            break;
        }
        case EShaderBindingType::SAMPLER:
        {
            SamplerDescriptors.push_back({});
            D3D12_DESCRIPTOR_RANGE1& range = SamplerDescriptors[SamplerDescriptors.size() - 1];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
            range.NumDescriptors = ShaderBinding->NumOfDescriptors;
            range.BaseShaderRegister = SamplerIndex;
            range.RegisterSpace = 0;
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

    D3D12_ROOT_PARAMETER1 rootParameter[2];
    rootParameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameter[0].DescriptorTable.NumDescriptorRanges = Descriptors.size();
    rootParameter[0].DescriptorTable.pDescriptorRanges = &Descriptors[0];

    rootParameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameter[1].DescriptorTable.NumDescriptorRanges = SamplerDescriptors.size();
    rootParameter[1].DescriptorTable.pDescriptorRanges = &SamplerDescriptors[0];

    // RootSignature 생성
    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = _countof(rootParameter);
    rootSignatureDesc.pParameters = rootParameter;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = { };
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1 = rootSignatureDesc;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (JFAIL_E(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error), error))
        return false;

    if (JFAIL(g_rhi_dx12->Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&RootSignature))))
        return false;

    return true;
}

jShaderBindingInstance* jShaderBindingsLayout_DX12::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
{
    auto ShaderBindingInstance = new jShaderBindingInstance_DX12();
    ShaderBindingInstance->ShaderBindingLayout = this;
    ShaderBindingInstance->Initialize(InShaderBindingArray);
    ShaderBindingInstance->SetType(InType);
    
    return ShaderBindingInstance;
}

