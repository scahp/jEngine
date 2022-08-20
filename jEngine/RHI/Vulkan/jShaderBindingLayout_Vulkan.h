#pragma once
#include "jRHIType_Vulkan.h"

struct jWriteDescriptorInfo
{
    VkDescriptorBufferInfo BufferInfo{};
    VkDescriptorImageInfo ImageInfo{};
};

struct jWriteDescriptorSet
{
    void Reset()
    {
        WriteDescriptorInfos.clear();
        DescriptorWrites.clear();
        IsInitialized = false;
    }

    void SetWriteDescriptorInfo(int32 InIndex, const jShaderBinding& InShaderBinding);

    bool IsInitialized = false;
    std::vector<jWriteDescriptorInfo> WriteDescriptorInfos;
    std::vector<VkWriteDescriptorSet> DescriptorWrites;         // 이게 최종 결과임, WriteDescriptorInfos 를 사용하여 생성함.
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingInstance_Vulkan : public jShaderBindingInstance
{
    virtual ~jShaderBindingInstance_Vulkan();

    static void CreateWriteDescriptorSet(jWriteDescriptorSet& OutWriteDescriptorSet
        , const VkDescriptorSet InDescriptorSet, const std::vector<jShaderBinding>& InShaderBindings);
    static void UpdateWriteDescriptorSet(
        jWriteDescriptorSet& OutDescriptorWrites, const std::vector<jShaderBinding>& InShaderBindings);

    virtual void Initialize(const std::vector<jShaderBinding>& InShaderBindings) override;
    virtual void UpdateShaderBindings(const std::vector<jShaderBinding>& InShaderBindings) override;
    virtual void BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;
    virtual void BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const override;

    VkDescriptorSet DescriptorSet = nullptr;
    jWriteDescriptorSet WriteDescriptorSet;
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingLayout_Vulkan : public jShaderBindingsLayout
{
    VkDescriptorSetLayout DescriptorSetLayout = nullptr;

    virtual bool Initialize(const std::vector<jShaderBinding>& shaderBindings) override;

    virtual jShaderBindingInstance* CreateShaderBindingInstance(const std::vector<jShaderBinding>& InShaderBindings) const override;

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