#pragma once
#include "jRHIType_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingInstance_Vulkan : public jShaderBindingInstance
{
    VkDescriptorSet DescriptorSet = nullptr;

    virtual void UpdateShaderBindings(std::vector<jShaderBinding> InShaderBindings) override;
    virtual void BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;
    virtual void BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindings_Vulkan : public jShaderBindings
{
    VkDescriptorSetLayout DescriptorSetLayout = nullptr;

    // Descriptor : 쉐이더가 버퍼나 이미지 같은 리소스에 자유롭게 접근하는 방법. 디스크립터의 사용방법은 아래 3가지로 구성됨.
    //	1. Pipeline 생성 도중 Descriptor Set Layout 명세
    //	2. Descriptor Pool로 Descriptor Set 생성
    //	3. Descriptor Set을 렌더링 하는 동안 묶어 주기.
    //
    // Descriptor set layout	: 파이프라인을 통해 접근할 리소스 타입을 명세함
    // Descriptor set			: Descriptor 에 묶일 실제 버퍼나 이미지 리소스를 명세함.
    VkDescriptorPool DescriptorPool = nullptr;

    mutable std::vector<jShaderBindingInstance_Vulkan*> CreatedBindingInstances;

    virtual bool CreateDescriptorSetLayout() override;
    virtual void CreatePool() override;

    virtual jShaderBindingInstance* CreateShaderBindingInstance() const override;
    virtual std::vector<jShaderBindingInstance*> CreateShaderBindingInstance(int32 count) const override;

    virtual size_t GetHash() const override;

    std::vector<VkDescriptorPoolSize> GetDescriptorPoolSizeArray(uint32 maxAllocations) const
    {
        std::vector<VkDescriptorPoolSize> resultArray;

        if (!ShaderBindings.empty())
        {
            uint32 NumOfSameType = 0;
            VkDescriptorType PrevType = GetVulkanShaderBindingType(ShaderBindings[0].BindingType);
            for (int32 i = 0; i < (int32)ShaderBindings.size(); ++i)
            {
                if (PrevType == GetVulkanShaderBindingType(ShaderBindings[i].BindingType))
                {
                    ++NumOfSameType;
                }
                else
                {
                    VkDescriptorPoolSize poolSize;
                    poolSize.type = PrevType;
                    poolSize.descriptorCount = NumOfSameType * maxAllocations;
                    resultArray.push_back(poolSize);

                    PrevType = GetVulkanShaderBindingType(ShaderBindings[i].BindingType);
                    NumOfSameType = 1;
                }
            }

            if (NumOfSameType > 0)
            {
                VkDescriptorPoolSize poolSize;
                poolSize.type = PrevType;
                poolSize.descriptorCount = NumOfSameType * maxAllocations;
                resultArray.push_back(poolSize);
            }
        }

        return std::move(resultArray);
    }
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindingsManager_Vulkan
//////////////////////////////////////////////////////////////////////////
class jShaderBindingsManager_Vulkan // base 없음
{
public:
    VkDescriptorPool CreatePool(const jShaderBindings_Vulkan& bindings, uint32 MaxAllocations = 32) const;
    void Release(VkDescriptorPool pool) const;
};