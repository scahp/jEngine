#pragma once
#include "../jRHIType.h"

const float DefaultPoolSizes[] =
{
    2,		    // VK_DESCRIPTOR_TYPE_SAMPLER
    2,		    // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    2,		    // VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    1 / 2.0,	// VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
    1 / 4.0,	// VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    4,		    // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
    1 / 8.0	    // VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
};

//using jShaderBindingInstanceVulkanArray = jResourceContainer<jShaderBindingInstance_Vulkan*, 2000>;
using jShaderBindingInstanceVulkanArray = std::vector<std::shared_ptr<jShaderBindingInstance_Vulkan>>;

struct jDescriptorPool_Vulkan
{
    std::map<VkDescriptorSetLayout, jShaderBindingInstanceVulkanArray> PendingDescriptorSets;
    std::map<VkDescriptorSetLayout, jShaderBindingInstanceVulkanArray> AllocatedDescriptorSets;

    jDescriptorPool_Vulkan() = default;
    virtual ~jDescriptorPool_Vulkan();

    virtual void Create(uint32 InMaxDescriptorSets = 128);
    virtual void Reset();
    virtual std::shared_ptr<jShaderBindingInstance_Vulkan> AllocateDescriptorSet(VkDescriptorSetLayout InLayout);
    virtual void Free(std::shared_ptr<jShaderBindingInstance_Vulkan> InShaderBindingInstance);
    void Release();

    uint32 MaxDescriptorSets = 128;
    uint32 PoolSizes[_countof(DefaultPoolSizes)];
    VkDescriptorPool DescriptorPool = nullptr;;
    // mutable jMutexLock DescriptorPoolLock;
    mutable jMutexLock DescriptorPoolLock;

    static constexpr int32 NumOfFramesToWaitBeforeReleasing = 3;
    struct jPendingFreeShaderBindingInstance
    {
        jPendingFreeShaderBindingInstance() = default;
        jPendingFreeShaderBindingInstance(int32 InFrameIndex, std::shared_ptr<jShaderBindingInstance_Vulkan> InShaderBindingInstance) : FrameIndex(InFrameIndex), ShaderBindingInstance(InShaderBindingInstance) {}

        int32 FrameIndex = 0;
        std::shared_ptr<jShaderBindingInstance_Vulkan> ShaderBindingInstance = nullptr;
    };
    std::vector<jPendingFreeShaderBindingInstance> PendingFree;
    int32 CanReleasePendingFreeShaderBindingInstanceFrameNumber = 0;
};

//// The Pool is called by Heap in DX12.
//struct jDescriptorPool_DX12
//{
//    //robin_hood::unordered_map<VkDescriptorSetLayout, jShaderBindingInstanceVulkanArray> PendingDescriptorSets;
//    //robin_hood::unordered_map<VkDescriptorSetLayout, jShaderBindingInstanceVulkanArray> AllocatedDescriptorSets;
//
//    jDescriptorPool_DX12() = default;
//    virtual ~jDescriptorPool_DX12() {}
//
//    virtual void Create(uint32 InMaxDescriptorSets = 128)
//    {
//        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = { };
//        heapDesc.NumDescriptors = InMaxDescriptorSets;
//        heapDesc.Type = HeapType;
//        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
//
//        const bool IsShaderVisible = true;
//        if (IsShaderVisible)
//            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
//
//        check(g_rhi_dx12);
//        check(g_rhi_dx12->Device);
//        if (JFAIL(g_rhi_dx12->Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&DescriptorHeap))))
//            return;
//        CPUDescStart = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
//        GPUDescStart = DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
//
//        DescriptorSize = g_rhi_dx12->Device->GetDescriptorHandleIncrementSize(HeapType);
//    }
//    //virtual void Reset()
//    //{
//
//    //}
//    //virtual jShaderBindingInstance_DX12* AllocateDescriptorSet(VkDescriptorSetLayout InLayout);
//    //virtual void Free(jShaderBindingInstance* InShaderBindingInstance);
//    //void Release();
//
//    uint32 MaxDescriptorSets = 128;
//    uint32 PoolSizes[_countof(DefaultPoolSizes)];
//    ComPtr<ID3D12DescriptorHeap> DescriptorHeap;
//    D3D12_DESCRIPTOR_HEAP_TYPE HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
//    D3D12_CPU_DESCRIPTOR_HANDLE CPUDescStart = { };
//    D3D12_GPU_DESCRIPTOR_HANDLE GPUDescStart = { };
//    uint32 DescriptorSize = 0;
//    // mutable jMutexLock DescriptorPoolLock;
//    mutable jMutexLock DescriptorPoolLock;
//
//    static constexpr int32 NumOfFramesToWaitBeforeReleasing = 3;
//    struct jPendingFreeShaderBindingInstance
//    {
//        jPendingFreeShaderBindingInstance() = default;
//        jPendingFreeShaderBindingInstance(int32 InFrameIndex, jShaderBindingInstance* InShaderBindingInstance) : FrameIndex(InFrameIndex), ShaderBindingInstance(InShaderBindingInstance) {}
//
//        int32 FrameIndex = 0;
//        jShaderBindingInstance* ShaderBindingInstance = nullptr;
//    };
//    std::vector<jPendingFreeShaderBindingInstance> PendingFree;
//    int32 CanReleasePendingFreeShaderBindingInstanceFrameNumber = 0;
//};