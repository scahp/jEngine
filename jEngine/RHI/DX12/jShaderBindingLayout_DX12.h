#pragma once
#include "../jShaderBindingLayout.h"

struct jRootParameterExtractor
{
public:
    std::vector<D3D12_ROOT_PARAMETER1> RootParameters;
    std::vector<D3D12_DESCRIPTOR_RANGE1> Descriptors;
    std::vector<D3D12_DESCRIPTOR_RANGE1> SamplerDescriptors;

    void Extract(const jShaderBindingLayoutArray& InBindingLayoutArray, int32 InRegisterSpace = 0);    
    void Extract(const jShaderBindingInstanceArray& InBindingLayoutArray, int32 InRegisterSpace = 0);

protected:
    void Extract(int32& InOutDescriptorOffset, int32& InOutSamplerDescriptorOffset, const jShaderBindingArray& InShaderBindingArray, int32 InRegisterSpace = 0);

private:
    int32 NumOfInlineRootParameter = 0;    
};

struct jShaderBindingLayout_DX12 : public jShaderBindingLayout
{
    virtual ~jShaderBindingLayout_DX12() {}

    virtual bool Initialize(const jShaderBindingArray& InShaderBindingArray) override;
    virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const override;

    virtual void* GetHandle() const { return nullptr; }

    std::vector<D3D12_ROOT_PARAMETER1> RootParameters;
    std::vector<D3D12_DESCRIPTOR_RANGE1> Descriptors;
    std::vector<D3D12_DESCRIPTOR_RANGE1> SamplerDescriptors;

    static robin_hood::unordered_map<size_t, ComPtr<ID3D12RootSignature>> GRootSignaturePool;
    static jMutexRWLock GRootSignatureLock;

    //////////////////////////////////////////////////////////////////////////
    // RootSignature extractor utility
    using FuncGetRootParameterExtractor = std::function<void(jRootParameterExtractor&)>;

    static ID3D12RootSignature* CreateRootSignatureInternal(size_t InHash, FuncGetRootParameterExtractor InFunc);
    static ID3D12RootSignature* CreateRootSignature(const jShaderBindingInstanceArray& InBindingInstanceArray);
    static ID3D12RootSignature* CreateRootSignature(const jShaderBindingLayoutArray& InBindingLayoutArray);
};

