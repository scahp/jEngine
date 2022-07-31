#pragma once

struct IBuffer
{
    virtual ~IBuffer() {}

    virtual jName GetName() const { return jName::Invalid; }
    virtual void Bind(const jShader* shader) const = 0;
};

struct jVertexBuffer : public IBuffer
{
    std::shared_ptr<jVertexStreamData> VertexStreamData;

    virtual size_t GetHash() const { return 0; }
    virtual void Bind(const jShader* shader) const {}
    virtual void Bind() const {}
};

struct jIndexBuffer : public IBuffer
{
    std::shared_ptr<jIndexStreamData> IndexStreamData;

    virtual void Bind(const jShader* shader) const {}
    virtual void Bind() const {}
};