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
    virtual void BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;
    virtual void BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;
    virtual void* GetHandle() const override;
    virtual const std::vector<uint32>* GetDynamicOffsets() const override { return 0; }
    virtual void Free() override {}

    void BindGraphics(jCommandBuffer_DX12* InCommandList);
    void BindCompute(jCommandBuffer_DX12* InCommandList);
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

    std::vector<std::pair<jInlineRootParamType::Enum, D3D12_GPU_VIRTUAL_ADDRESS>> RootParameterInlines;

    std::vector<jDescriptor_DX12> Descriptors;
    std::vector<jDescriptor_DX12> SamplerDescriptors;
    const jShaderBindingsLayout_DX12* ShaderBindingLayout = nullptr;
};
