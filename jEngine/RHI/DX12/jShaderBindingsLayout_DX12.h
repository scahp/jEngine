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
    //virtual void* GetHandle() const { return RootSignature.Get(); }
    virtual void* GetHandle() const { return nullptr; }

    //ID3D12RootSignature* GetRootSignature() const { return RootSignature.Get(); }

    mutable size_t Hash = 0;

    //ComPtr<ID3D12RootSignature> RootSignature;
    std::vector<D3D12_ROOT_PARAMETER1> RootParameters;
    std::vector<D3D12_DESCRIPTOR_RANGE1> Descriptors;
    std::vector<D3D12_DESCRIPTOR_RANGE1> SamplerDescriptors;

    static void GetRootParameters(std::vector<D3D12_ROOT_PARAMETER1>& OutRootParameters, std::vector<D3D12_DESCRIPTOR_RANGE1>& OutDescriptors
        , std::vector<D3D12_DESCRIPTOR_RANGE1>& OutSamplerDescriptors, const jShaderBindingArray& InShaderBindingArray, int32 InRegisterSpace);

    static robin_hood::unordered_map<size_t, ComPtr<ID3D12RootSignature>> GRootSignaturePool;
    static jMutexRWLock GRootSignatureLock;

    static ID3D12RootSignature* CreateRootSignature(const jShaderBindingInstanceArray& InBindingInstanceArray);
    static ID3D12RootSignature* CreateRootSignature(const jShaderBindingsLayoutArray& InBindingLayoutArray);

protected:
    jShaderBindingArray ShaderBindingArray;     // Resource 정보는 비어있음
};
