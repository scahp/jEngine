﻿#pragma once

struct jShader;

struct jVertexBuffer
{
    std::shared_ptr<jVertexStreamData> VertexStreamData;

    virtual ~jVertexBuffer() {}
    virtual jName GetName() const { return jName::Invalid; }
    virtual size_t GetHash() const { return 0; }
    virtual void Bind(const jShader* shader) const {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const {}
    virtual int32 GetElementCount() const { return VertexStreamData ? VertexStreamData->ElementCount : 0; }
    virtual bool Initialize(const std::shared_ptr<jVertexStreamData>& InStreamData) { return false; }
    virtual bool IsSupportRaytracing() const { return false; }
    virtual jBuffer* GetBuffer(int32 InStreamIndex) const { return nullptr; }
};

struct jIndexBuffer
{
    std::shared_ptr<jIndexStreamData> IndexStreamData;

    virtual ~jIndexBuffer() {}
    virtual jName GetName() const { return jName::Invalid; }
    virtual void Bind(const jShader* shader) const {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const {}
    virtual int32 GetElementCount() const { return IndexStreamData ? IndexStreamData->ElementCount : 0; }
    virtual bool Initialize(const std::shared_ptr<jIndexStreamData>& InStreamData) { return false; }
    virtual jBuffer* GetBuffer() const { return nullptr;  }
};

struct jVertexBufferArray : public jResourceContainer<const jVertexBuffer*>
{
    size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = 0;
        for (int32 i = 0; i < NumOfData; ++i)
        {
            Hash ^= (Data[i]->GetHash() << i);
        }
        return Hash;
    }

private:
    mutable size_t Hash = 0;
};
