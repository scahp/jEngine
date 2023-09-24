#pragma once
#include "../jBuffer.h"

struct jBuffer_DX12;

struct jIndexBuffer_DX12 : public jIndexBuffer
{
    std::shared_ptr<jBuffer_DX12> BufferPtr;

    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;
    virtual void Bind(jCommandBuffer_DX12* InCommandList) const;
    virtual bool Initialize(const std::shared_ptr<jIndexStreamData>& InStreamData) override;

    D3D12_INDEX_BUFFER_VIEW IBView;

    FORCEINLINE uint32 GetIndexCount() const
    {
        return IndexStreamData->ElementCount;
    }
};

