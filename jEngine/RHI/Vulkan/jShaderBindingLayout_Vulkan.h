#pragma once
#include "jRHIType_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingInstance_Vulkan : public jShaderBindingInstance
{
    virtual ~jShaderBindingInstance_Vulkan();

    VkDescriptorSet DescriptorSet = nullptr;

    virtual void UpdateShaderBindings(const std::vector<jShaderBinding>& InShaderBindings) override;
    virtual void BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;
    virtual void BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingLayout_Vulkan : public jShaderBindingLayout
{
    VkDescriptorSetLayout DescriptorSetLayout = nullptr;

    virtual bool CreateDescriptorSetLayout() override;

    virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance() const override;
    virtual std::vector<std::shared_ptr<jShaderBindingInstance>> CreateShaderBindingInstance(int32 count) const override;

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
    VkDescriptorPool CreatePool(const jShaderBindingLayout_Vulkan& bindings, uint32 MaxAllocations = 32) const;
    void Release(VkDescriptorPool pool) const;
};