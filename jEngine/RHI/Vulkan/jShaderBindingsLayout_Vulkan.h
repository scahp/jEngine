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
        DynamicOffsets.clear();
        IsInitialized = false;
    }

    void SetWriteDescriptorInfo(int32 InIndex, const jShaderBinding* InShaderBinding);

    bool IsInitialized = false;
    std::vector<jWriteDescriptorInfo> WriteDescriptorInfos;
    std::vector<VkWriteDescriptorSet> DescriptorWrites;         // 이게 최종 결과임, WriteDescriptorInfos 를 사용하여 생성함.
    std::vector<uint32> DynamicOffsets;
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingInstance_Vulkan : public jShaderBindingInstance, public std::enable_shared_from_this<jShaderBindingInstance_Vulkan>
{
    virtual ~jShaderBindingInstance_Vulkan();

    static void CreateWriteDescriptorSet(jWriteDescriptorSet& OutWriteDescriptorSet
        , const VkDescriptorSet InDescriptorSet, const jShaderBindingArray& InShaderBindingArray);
    static void UpdateWriteDescriptorSet(
        jWriteDescriptorSet& OutDescriptorWrites, const jShaderBindingArray& InShaderBindingArray);

    virtual void Initialize(const jShaderBindingArray& InShaderBindingArray) override;
    virtual void UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray) override;
    virtual void* GetHandle() const override { return DescriptorSet; }
    virtual const std::vector<uint32>* GetDynamicOffsets() const override { return &WriteDescriptorSet.DynamicOffsets; }
    virtual void Free() override;

    VkDescriptorSet DescriptorSet = nullptr;        // DescriptorPool 을 해제하면 모두 처리될 수 있어서 따로 소멸시키지 않음
    jWriteDescriptorSet WriteDescriptorSet;
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingsLayout_Vulkan : public jShaderBindingsLayout
{
    virtual ~jShaderBindingsLayout_Vulkan();

    VkDescriptorSetLayout DescriptorSetLayout = nullptr;

    virtual bool Initialize(const jShaderBindingArray& InShaderBindingArray) override;
    virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const override;
    virtual size_t GetHash() const override;
    void Release();
    virtual void* GetHandle() const override { return DescriptorSetLayout; }

    std::vector<VkDescriptorPoolSize> GetDescriptorPoolSizeArray(uint32 maxAllocations) const
    {
        std::vector<VkDescriptorPoolSize> resultArray;

        if (!ShaderBindingArray.NumOfData)
        {
            uint32 NumOfSameType = 0;
            VkDescriptorType PrevType = GetVulkanShaderBindingType(ShaderBindingArray[0]->BindingType);
            for (int32 i = 0; i < ShaderBindingArray.NumOfData; ++i)
            {
                if (PrevType == GetVulkanShaderBindingType(ShaderBindingArray[i]->BindingType))
                {
                    ++NumOfSameType;
                }
                else
                {
                    VkDescriptorPoolSize poolSize;
                    poolSize.type = PrevType;
                    poolSize.descriptorCount = NumOfSameType * maxAllocations;
                    resultArray.push_back(poolSize);

                    PrevType = GetVulkanShaderBindingType(ShaderBindingArray[i]->BindingType);
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

public:
    static VkDescriptorSetLayout CreateDescriptorSetLayout(const jShaderBindingArray& InShaderBindingArray);
    static VkPipelineLayout CreatePipelineLayout(const jShaderBindingsLayoutArray& InShaderBindingLayoutArray, const jPushConstant* pushConstant);
    static void ClearPipelineLayout();

    static jMutexRWLock PipelineLayoutPoolLock;
    static robin_hood::unordered_map<size_t, VkPipelineLayout> PipelineLayoutPool;
};
