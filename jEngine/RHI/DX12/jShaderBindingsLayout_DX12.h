#pragma once
#include "../jShaderBindingsLayout.h"

struct jShaderBindingsLayout_DX12 : public jShaderBindingsLayout
{
    virtual ~jShaderBindingsLayout_DX12() {}

    virtual bool Initialize(const jShaderBindingArray& InShaderBindingArray) override;
    virtual jShaderBindingInstance* CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const override;
    virtual size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = ShaderBindingArray.GetHash();
        return Hash;
    }
    virtual const jShaderBindingArray& GetShaderBindingsLayout() const { return ShaderBindingArray; }
    virtual void* GetHandle() const { return RootSignature.Get(); }

    mutable size_t Hash = 0;

    ComPtr<ID3D12RootSignature> RootSignature;

protected:
    jShaderBindingArray ShaderBindingArray;     // Resource 정보는 비어있음
};
