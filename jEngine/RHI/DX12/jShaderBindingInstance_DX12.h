#pragma once

struct jShaderBindingsLayout_DX12;

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_DX12
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingInstance_DX12 : public jShaderBindingInstance
{
    virtual ~jShaderBindingInstance_DX12() {}

    virtual void Initialize(const jShaderBindingArray& InShaderBindingArray) override;
    virtual void UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray) override;
    virtual void* GetHandle() const override;
    virtual const std::vector<uint32>* GetDynamicOffsets() const override { return 0; }
    virtual void Free() override {}

    void BindGraphics(jCommandBuffer_DX12* InCommandList, int32& InOutStartIndex) const;
    void BindCompute(jCommandBuffer_DX12* InCommandList, int32& InOutStartIndex);
    void CopyToOnlineDescriptorHeap(jCommandBuffer_DX12* InCommandList);

    struct jInlineRootParamType
    {
        enum Enum
        {
            CBV = 0,
            SRV,
            UAV,
            NumOfType
        };
    };

    struct jInlineRootParamData
    {
        jInlineRootParamType::Enum Type = jInlineRootParamType::NumOfType;
        D3D12_GPU_VIRTUAL_ADDRESS GPUVirtualAddress = {};

        // todo : Debug 정보라 런타임에서 제외 되면 좋을 듯 함.
        jName ResourceName;
        const jShaderBindableResource* Resource = nullptr;
    };

    struct jDescriptorData
    {
        FORCEINLINE bool IsValid() const { return Descriptor.IsValid(); }
        jDescriptor_DX12 Descriptor;

        // todo : Debug 정보라 런타임에서 제외 되면 좋을 듯 함.
        jName ResourceName;
        const jShaderBindableResource* Resource = nullptr;
    };

    std::vector<jInlineRootParamData> RootParameterInlines;
    std::vector<jDescriptorData> Descriptors;
    std::vector<jDescriptorData> SamplerDescriptors;
};
