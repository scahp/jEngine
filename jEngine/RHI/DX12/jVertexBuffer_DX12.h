#pragma once
#include "jRHIType_DX12.h"
#include "jBuffer_DX12.h"

struct jCommandBuffer_DX12;

struct jVertexStream_DX12
{
    jName Name;
    //uint32 Count;
    EBufferType BufferType = EBufferType::STATIC;
    //EBufferElementType ElementType = EBufferElementType::BYTE;
    bool Normalized = false;
    int32 Stride = 0;
    size_t Offset = 0;
    int32 InstanceDivisor = 0;

    std::shared_ptr<jBuffer_DX12> BufferPtr;
};

struct jVertexBuffer_DX12 : public jVertexBuffer
{
    struct jBindInfo
    {
        void Reset()
        {
            InputElementDescs.clear();
            //Buffers.clear();
            //Offsets.clear();
            StartBindingIndex = 0;
        }
        std::vector<D3D12_INPUT_ELEMENT_DESC> InputElementDescs;

        // Buffer 는 jVertexStream_DX12 을 레퍼런스 하고 있기 때문에 별도 소멸 필요 없음
        std::vector<ID3D12Resource*> Buffers;
        std::vector<size_t> Offsets;

        int32 StartBindingIndex = 0;

        D3D12_INPUT_LAYOUT_DESC CreateVertexInputLayoutDesc() const
        {
            D3D12_INPUT_LAYOUT_DESC Desc;
            Desc.pInputElementDescs = InputElementDescs.data();
            Desc.NumElements = (uint32)InputElementDescs.size();
            return Desc;
        }

        FORCEINLINE size_t GetHash() const
        {
            size_t result = CityHash64((const char*)InputElementDescs.data()
                , sizeof(D3D12_INPUT_ELEMENT_DESC) * InputElementDescs.size());
            return result;
        }
    };

    virtual size_t GetHash() const override
    {
        if (Hash)
            return Hash;

        Hash = GetVertexInputStateHash() ^ (size_t)VertexStreamData->PrimitiveType;
        return Hash;
    }

    FORCEINLINE size_t GetVertexInputStateHash() const
    {
        return BindInfos.GetHash();
    }

    FORCEINLINE D3D12_INPUT_LAYOUT_DESC CreateVertexInputLayoutDesc() const
    {
        return BindInfos.CreateVertexInputLayoutDesc();
    }

    FORCEINLINE D3D12_PRIMITIVE_TOPOLOGY_TYPE GetTopologyType() const
    {
        return GetDX12PrimitiveTopology(VertexStreamData->PrimitiveType);
    }

    static void CreateVertexInputState(std::vector<D3D12_INPUT_ELEMENT_DESC>& OutInputElementDescs, 
        const jVertexBufferArray& InVertexBufferArray)
    {
        for (int32 i = 0; i < InVertexBufferArray.NumOfData; ++i)
        {
            check(InVertexBufferArray[i]);
            const jBindInfo& bindInfo = ((const jVertexBuffer_DX12*)InVertexBufferArray[i])->BindInfos;
            OutInputElementDescs.insert(OutInputElementDescs.end(), bindInfo.InputElementDescs.begin(), bindInfo.InputElementDescs.end());
        }
    }

    jBindInfo BindInfos;
    std::vector<jVertexStream_DX12> Streams;
    std::vector<D3D12_VERTEX_BUFFER_VIEW> VBView;
    mutable size_t Hash = 0;

    virtual bool Initialize(const std::shared_ptr<jVertexStreamData>& InStreamData) override;
    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;
    virtual void Bind(jCommandBuffer_DX12* InCommandList);
};
