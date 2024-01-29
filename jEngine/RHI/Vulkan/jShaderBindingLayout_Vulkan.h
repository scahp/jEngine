#pragma once
#include "jRHIType_Vulkan.h"

struct jWriteDescriptorInfo
{
    jWriteDescriptorInfo() = default;
    jWriteDescriptorInfo(VkDescriptorBufferInfo InBufferInfo) : BufferInfo(InBufferInfo) {}
    jWriteDescriptorInfo(VkDescriptorImageInfo InImageInfo) : ImageInfo(InImageInfo) {}
    jWriteDescriptorInfo(VkWriteDescriptorSetAccelerationStructureKHR InAccelerationStructureInfo) : AccelerationStructureInfo(InAccelerationStructureInfo) {}

    VkDescriptorBufferInfo BufferInfo{};
    VkDescriptorImageInfo ImageInfo{};
    VkWriteDescriptorSetAccelerationStructureKHR AccelerationStructureInfo{};
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

    void SetWriteDescriptorInfo(const jShaderBinding* InShaderBinding);

    bool IsInitialized = false;
    std::vector<jWriteDescriptorInfo> WriteDescriptorInfos;
    std::vector<VkWriteDescriptorSet> DescriptorWrites;         // 이게 최종 결과임, WriteDescriptorInfos 를 사용하여 생성함.
    std::vector<uint32> DynamicOffsets;
};

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingInstance_Vulkan : public jShaderBindingInstance
{
    virtual ~jShaderBindingInstance_Vulkan();

    static void CreateWriteDescriptorSet(jWriteDescriptorSet& OutWriteDescriptorSet
        , const VkDescriptorSet InDescriptorSet, const jShaderBindingArray& InShaderBindingArray);
    //static void UpdateWriteDescriptorSet(
    //    jWriteDescriptorSet& OutDescriptorWrites, const jShaderBindingArray& InShaderBindingArray);

    virtual void Initialize(const jShaderBindingArray& InShaderBindingArray) override;
    virtual void UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray) override;
    virtual void* GetHandle() const override { return DescriptorSet; }
    virtual const std::vector<uint32>* GetDynamicOffsets() const override { return &WriteDescriptorSet.DynamicOffsets; }
    virtual void Free() override;

    VkDescriptorSet DescriptorSet = nullptr;        // DescriptorPool 을 해제하면 모두 처리될 수 있어서 따로 소멸시키지 않음
    jWriteDescriptorSet WriteDescriptorSet;
};

//////////////////////////////////////////////////////////////////////////
// jShaderBinding_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jShaderBindingLayout_Vulkan : public jShaderBindingLayout
{
    virtual ~jShaderBindingLayout_Vulkan();

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
    static VkPipelineLayout CreatePipelineLayout(const jShaderBindingLayoutArray& InShaderBindingLayoutArray, const jPushConstant* InPushConstant, EShaderAccessStageFlag InShaderAccessStageFlag);
    static void ClearPipelineLayout();

    static jMutexRWLock DescriptorLayoutPoolLock;
    static robin_hood::unordered_map<size_t, VkDescriptorSetLayout> DescriptorLayoutPool;

    static jMutexRWLock PipelineLayoutPoolLock;
    static robin_hood::unordered_map<size_t, VkPipelineLayout> PipelineLayoutPool;
};
