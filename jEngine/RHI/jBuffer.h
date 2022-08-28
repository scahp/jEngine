#pragma once

struct jVertexBuffer
{
    std::shared_ptr<jVertexStreamData> VertexStreamData;

    virtual ~jVertexBuffer() {}
    virtual jName GetName() const { return jName::Invalid; }
    virtual size_t GetHash() const { return 0; }
    virtual void Bind(const jShader* shader) const {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const {}
    virtual int32 GetElementCount() const { return VertexStreamData ? VertexStreamData->ElementCount : 0; }
};

struct jIndexBuffer
{
    std::shared_ptr<jIndexStreamData> IndexStreamData;

    virtual ~jIndexBuffer() {}
    virtual jName GetName() const { return jName::Invalid; }
    virtual void Bind(const jShader* shader) const {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const {}
    virtual int32 GetElementCount() const { return IndexStreamData ? IndexStreamData->ElementCount : 0; }
};