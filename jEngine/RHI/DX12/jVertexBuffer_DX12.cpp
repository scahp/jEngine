#include "pch.h"
#include "jVertexBuffer_DX12.h"
#include "../jRenderFrameContext.h"
#include "jBufferUtil_DX12.h"

bool jVertexBuffer_DX12::Initialize(const std::shared_ptr<jVertexStreamData>& InStreamData)
{
    if (!InStreamData)
        return false;

    VertexStreamData = InStreamData;
    BindInfos.Reset();
    BindInfos.StartBindingIndex = InStreamData->BindingIndex;

    Streams.clear();
    Streams.reserve(InStreamData->Params.size());

    VBView.clear();
    VBView.reserve(InStreamData->Params.size());

    std::list<uint32> buffers;
    int32 locationIndex = InStreamData->StartLocation;
    int32 bindingIndex = InStreamData->BindingIndex;
    for (const auto& iter : InStreamData->Params)
    {
        if (iter->Stride <= 0)
            continue;

        jVertexStream_DX12 stream;
        stream.Name = iter->Name;
        stream.BufferType = EBufferType::STATIC;
        stream.Stride = iter->Stride;
        stream.Offset = 0;
        stream.InstanceDivisor = iter->InstanceDivisor;

        // Create vertex buffer
        stream.BufferPtr = std::shared_ptr<jBuffer_DX12>(jBufferUtil_DX12::CreateBuffer(iter->GetBufferSize(), 0, EBufferCreateFlag::NONE, D3D12_RESOURCE_STATE_COMMON
            , iter->GetBufferData(), iter->GetBufferSize(), TEXT("VertexBuffer")));
        //jBufferUtil_DX12::CreateShaderResourceView(stream.BufferPtr.get(), (uint32)iter->Stride, (uint32)(iter->GetBufferSize() / iter->Stride));     // As StructuredBuffer
        jBufferUtil_DX12::CreateShaderResourceView(stream.BufferPtr.get(), 0, (uint32)(iter->GetBufferSize() / 4));      // As Raw

        int32 ElementStride = 0;
        for (IStreamParam::jAttribute& element : iter->Attributes)
        {
            DXGI_FORMAT AttrFormat = DXGI_FORMAT_UNKNOWN;
            switch (element.UnderlyingType)
            {
            case EBufferElementType::BYTE:
            {
                const int32 elementCount = element.Stride / sizeof(char);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = DXGI_FORMAT_R8_SINT;
                    break;
                case 2:
                    AttrFormat = DXGI_FORMAT_R8G8_SINT;
                    break;
                case 3:
                    check(0);
                    // 어떻게 핸들링 할까? 타입이 업네?
                    // AttrFormat = DXGI_FORMAT_R8G8B8A8_SINT;
                    break;
                case 4:
                    AttrFormat = DXGI_FORMAT_R8G8B8A8_SINT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::BYTE_UNORM:
            {
                const int32 elementCount = element.Stride / sizeof(char);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = DXGI_FORMAT_R8_UNORM;
                    break;
                case 2:
                    AttrFormat = DXGI_FORMAT_R8G8_UNORM;
                    break;
                case 3:
                    // 어떻게 핸들링 할까? 타입이 없네?
                    // AttrFormat = DXGI_FORMAT_R8G8B8_UNORM;
                    break;
                case 4:
                    AttrFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::UINT16:
            {
                const int32 elementCount = element.Stride / sizeof(uint16);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = DXGI_FORMAT_R16_UINT;
                    break;
                case 2:
                    AttrFormat = DXGI_FORMAT_R16G16_UINT;
                    break;
                case 3:
                    // 어떻게 핸들링 할까? 타입이 없네?
                    // AttrFormat = DXGI_FORMAT_R16G16B16_UINT;
                    break;
                case 4:
                    AttrFormat = DXGI_FORMAT_R16G16B16A16_UINT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::UINT32:
            {
                const int32 elementCount = element.Stride / sizeof(uint32);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = DXGI_FORMAT_R32_UINT;
                    break;
                case 2:
                    AttrFormat = DXGI_FORMAT_R32G32_UINT;
                    break;
                case 3:
                    AttrFormat = DXGI_FORMAT_R32G32B32_UINT;
                    break;
                case 4:
                    AttrFormat = DXGI_FORMAT_R32G32B32A32_UINT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::FLOAT:
            {
                const int32 elementCount = element.Stride / sizeof(float);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = DXGI_FORMAT_R32_FLOAT;
                    break;
                case 2:
                    AttrFormat = DXGI_FORMAT_R32G32_FLOAT;
                    break;
                case 3:
                    AttrFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                    break;
                case 4:
                    AttrFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            default:
                check(0);
                break;
            }
            check(AttrFormat != VK_FORMAT_UNDEFINED);

            if (iter->GetBufferSize() > 0)
            {
                BindInfos.Buffers.push_back(stream.BufferPtr->Buffer.Get());
                BindInfos.Offsets.push_back(stream.Offset + stream.BufferPtr->Offset);
            }
        
            D3D12_INPUT_ELEMENT_DESC elem;
            elem.SemanticName = element.Name.GetStringLength() ? element.Name.ToStr() : iter->Name.ToStr();     // todo : remove iter-Name.ToStr();
            elem.SemanticIndex = 0;                     // todo
            elem.Format = AttrFormat;
            elem.InputSlot = bindingIndex;
            elem.AlignedByteOffset = ElementStride;
            elem.InputSlotClass = GetDX12VertexInputRate(InStreamData->VertexInputRate);
            elem.InstanceDataStepRate = (elem.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA) ? 1 : 0;
            BindInfos.InputElementDescs.emplace_back(elem);

            ElementStride += element.Stride;
            ++locationIndex;

        }

        Streams.emplace_back(stream);
        
        D3D12_VERTEX_BUFFER_VIEW view;
        view.BufferLocation = stream.BufferPtr->GetGPUAddress();
        view.SizeInBytes = (uint32)stream.BufferPtr->Size;
        view.StrideInBytes = stream.Stride;
        VBView.emplace_back(view);

        ++bindingIndex;
    }

    GetHash();		// GenerateHash

    return true;
}

void jVertexBuffer_DX12::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);
    
    Bind(CommandBuffer_DX12);
}

void jVertexBuffer_DX12::Bind(jCommandBuffer_DX12* InCommandList) const
{
    check(InCommandList->CommandList);
    InCommandList->CommandList->IASetPrimitiveTopology(GetTopology());
    InCommandList->CommandList->IASetVertexBuffers(BindInfos.StartBindingIndex, (uint32)VBView.size(), &VBView[0]);
}

bool jVertexBuffer_DX12::IsSupportRaytracing() const
{
    return GetDX12TextureComponentCount(GetDX12TextureFormat(BindInfos.InputElementDescs[0].Format)) >= 3;
}

jBuffer* jVertexBuffer_DX12::GetBuffer(int32 InStreamIndex) const
{
    check(Streams.size() > InStreamIndex); return Streams[InStreamIndex].BufferPtr.get();
}
