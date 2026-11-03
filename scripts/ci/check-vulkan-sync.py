#!/usr/bin/env python3
"""Run actual Vulkan RHI method bodies against a deterministic CPU driver stub.

This checks submission/fence and memory-barrier contracts without opening a
window or loading Vulkan. Hardware validation is still required for GPU use.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if not depth:
                return source[start:end + 1]
    raise ValueError(f'Unclosed method: {signature}')


STUBS = r'''
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>
#define check(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
#define ensure(value) (value)
#define USE_RESET_DESCRIPTOR_POOL 0
using uint8 = uint8_t;
using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;
template<class T> T handle(uintptr_t value) { return reinterpret_cast<T>(value); }
template<class T> T Align(T value, T alignment) { return (value + alignment - 1) / alignment * alignment; }
template<class T> T Max(T a, T b) { return std::max(a, b); }
template<class E> requires std::is_enum_v<E> E operator|(E a, E b) { return E(uint32(a) | uint32(b)); }
template<class E> requires std::is_enum_v<E> E operator&(E a, E b) { return E(uint32(a) & uint32(b)); }
template<class E> requires std::is_enum_v<E> E& operator|=(E& a, E b) { return a = a | b; }
template<class E> requires std::is_enum_v<E> bool operator!(E a) { return uint32(a) == 0; }
enum class ECommandBufferType { GRAPHICS, COMPUTE, COPY, MAX };
enum class ESemaphoreType { BINARY, TIMELINE };
enum class EResourceLayout { UAV, TRANSFER_SRC };
enum class EBufferCreateFlag : uint32 {
    CPUAccess = 1, Readback = 2, UAV = 4, AccelerationStructureBuildInput = 8,
    AccelerationStructure = 16, VertexBuffer = 32, IndexBuffer = 64,
    IndirectCommand = 128, ShaderBindingTable = 256
};
enum class EVulkanBufferBits : uint32 {
    SHADER_DEVICE_ADDRESS = 1, TRANSFER_DST = 2, TRANSFER_SRC = 4,
    STORAGE_BUFFER = 8, ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY = 16,
    ACCELERATION_STRUCTURE_STORAGE = 32, VERTEX_BUFFER = 64, INDEX_BUFFER = 128,
    INDIRECT_BUFFER = 256, SHADER_BINDING_TABLE = 512
};
enum class EVulkanMemoryBits : uint32 { DEVICE_LOCAL = 1, HOST_VISIBLE = 2, HOST_COHERENT = 4 };
struct jName {};
jName jNameStatic(const char*) { return {}; }
bool GSupportRaytracing = false;
bool IsRaytracingSceneSupported() { return false; }
struct jBuffer { virtual ~jBuffer() = default; };
struct jBuffer_Vulkan : jBuffer {
    VkBuffer Buffer = handle<VkBuffer>(0x700);
    VkAccelerationStructureKHR AccelerationStructure = VK_NULL_HANDLE;
    uint64 DeviceAddress = 0, Offset = 0, AllocatedSize = 0;
    std::vector<uint8> Bytes = std::vector<uint8>(1024);
    int Updates = 0;
    void UpdateBuffer(const void*, uint64) { ++Updates; }
    void* Map() { return Bytes.data(); }
    void Unmap() {}
};
struct jTexture {};
EVulkanMemoryBits LastMemoryBits{};
namespace jBufferUtil_Vulkan {
    std::shared_ptr<jBuffer_Vulkan> CreateBuffer(EVulkanBufferBits, EVulkanMemoryBits bits, uint64 size, EResourceLayout) {
        LastMemoryBits = bits;
        auto buffer = std::make_shared<jBuffer_Vulkan>();
        buffer->AllocatedSize = size;
        buffer->Bytes.resize(size);
        return buffer;
    }
    void CopyBuffer(jBuffer_Vulkan&, jBuffer_Vulkan&, uint64) {}
}
struct jResourceBarrierBatcher_Vulkan {
    VkMemoryBarrier Last{};
    VkPipelineStageFlags SourceStage = 0, DestStage = 0;
    void AddUAV(jBuffer*);
    void AddUAV(jTexture*);
    void AddBarrier(VkPipelineStageFlags src, VkPipelineStageFlags dst, VkMemoryBarrier barrier) {
        SourceStage = src; DestStage = dst; Last = barrier;
    }
};
struct jShaderBindingInstance { virtual ~jShaderBindingInstance() = default; };
struct jShaderBindingInstance_Vulkan : jShaderBindingInstance { VkDescriptorSet DescriptorSet = VK_NULL_HANDLE; };
using jShaderBindingInstancePtrArray = std::vector<std::shared_ptr<jShaderBindingInstance>>;
struct jScopedLock { explicit jScopedLock(int*) {} };
struct jDescriptorPool_Vulkan {
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    uint32 MaxDescriptorSets = 1, AllocatedDescriptorSetCount = 0;
    int DescriptorPoolLock = 0;
    std::map<VkDescriptorType, uint32> RemainingDescriptors;
    std::vector<VkDescriptorPool> OverflowDescriptorPools;
    std::map<VkDescriptorSetLayout, jShaderBindingInstancePtrArray> PendingDescriptorSets, AllocatedDescriptorSets;
    std::shared_ptr<jShaderBindingInstance> AllocateDescriptorSet(VkDescriptorSetLayout, const std::vector<VkDescriptorPoolSize>&);
    void Release();
};
struct DriverPool {
    std::map<VkDescriptorType, uint32> Remaining;
    uint32 Sets = 0, MaxSets = 0;
};
std::map<VkDescriptorPool, DriverPool> DriverPools;
std::map<VkDescriptorSetLayout, std::vector<VkDescriptorPoolSize>> DriverLayoutRequirements;
bool UnexpectedPoolOverrun = false, FailNextDescriptorAllocation = false;
uintptr_t NextPool = 0x1000, NextSet = 0x2000;
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo* info,
    const VkAllocationCallbacks*, VkDescriptorPool* result) {
    *result = handle<VkDescriptorPool>(NextPool++);
    auto& pool = DriverPools[*result]; pool.MaxSets = info->maxSets;
    for (uint32 i = 0; i < info->poolSizeCount; ++i)
        pool.Remaining[info->pPoolSizes[i].type] += info->pPoolSizes[i].descriptorCount;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice, VkDescriptorPool pool, const VkAllocationCallbacks*) { DriverPools.erase(pool); }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* result) {
    if (FailNextDescriptorAllocation) { FailNextDescriptorAllocation = false; return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    auto& pool = DriverPools.at(info->descriptorPool);
    const auto& requirements = DriverLayoutRequirements[*info->pSetLayouts];
    if (pool.Sets == pool.MaxSets) { UnexpectedPoolOverrun = true; return VK_ERROR_OUT_OF_POOL_MEMORY; }
    for (const auto& count : requirements)
        if (pool.Remaining[count.type] < count.descriptorCount) {
            UnexpectedPoolOverrun = true; return VK_ERROR_OUT_OF_POOL_MEMORY;
        }
    for (const auto& count : requirements) pool.Remaining[count.type] -= count.descriptorCount;
    ++pool.Sets;
    *result = handle<VkDescriptorSet>(NextSet++);
    return VK_SUCCESS;
}
struct jFence {
    VkFence Handle;
    int Waits = 0;
    void WaitForFence() { ++Waits; }
};
struct jCommandBuffer {
    ECommandBufferType Type = ECommandBufferType::GRAPHICS;
    jFence Fence{handle<VkFence>(0x100)};
    VkCommandBuffer Handle = handle<VkCommandBuffer>(0x200);
    void End() {}
    void* GetHandle() const { return Handle; }
    void* GetFenceHandle() const { return Fence.Handle; }
    jFence* GetFence() { return &Fence; }
};
std::vector<std::string> BuildEvents;
struct jCommandBuffer_Vulkan : jCommandBuffer {
    jResourceBarrierBatcher_Vulkan Barriers;
    bool PendingBarrier = false;
    const VkCommandBuffer& GetRef() const { return Handle; }
    void FlushBarrierBatch() {
        if (PendingBarrier) BuildEvents.push_back("barrier");
        PendingBarrier = false;
    }
};
struct jSemaphore {
    ESemaphoreType Type = ESemaphoreType::BINARY;
    VkSemaphore Handle = handle<VkSemaphore>(0x300);
    uint64 Value = 0;
    ESemaphoreType GetType() const { return Type; }
    void* GetHandle() const { return Handle; }
    uint64 GetValue() const { return Value; }
    uint64 IncrementValue() { return Type == ESemaphoreType::BINARY ? 1 : ++Value; }
};
struct jSemaphore_Vulkan : jSemaphore { VkSemaphore Semaphore = Handle; };
struct jSyncAcrossCommandQueue { virtual ~jSyncAcrossCommandQueue() = default; };
struct jSyncAcrossCommandQueue_Vulkan : jSyncAcrossCommandQueue {
    jSyncAcrossCommandQueue_Vulkan(ECommandBufferType, jSemaphore_Vulkan*, uint64 = uint64(-1));
    ECommandBufferType Type;
    jSemaphore_Vulkan* WaitSemaphore;
    uint64 SemaphoreValue;
};
struct jCommandBufferManager_Vulkan {
    jCommandBuffer_Vulkan Replacement;
    std::vector<std::shared_ptr<jSyncAcrossCommandQueue>> WaitSemaphoreAcrossQueues;
    void GetWaitSemaphoreAndValueThenClear(std::vector<VkSemaphore>&, std::vector<uint64>&);
    std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> QueueSubmit(jCommandBuffer_Vulkan*, jSemaphore*, jSemaphore*);
    void ReturnCommandBuffer(jCommandBuffer*) {}
    jCommandBuffer* GetOrCreateCommandBuffer() { return &Replacement; }
};
struct jSwapchainImage_Vulkan { VkFence CommandBufferFence = VK_NULL_HANDLE; };
struct jSwapchain_Vulkan { std::vector<jSwapchainImage_Vulkan*> Images; };
struct jRenderFrameContext_Vulkan;
VKAPI_ATTR VkDeviceAddress VKAPI_CALL FakeBufferAddress(VkDevice, const VkBufferDeviceAddressInfo*) { return 0x80000; }
VKAPI_ATTR void VKAPI_CALL FakeBuildSizes(VkDevice, VkAccelerationStructureBuildTypeKHR,
    const VkAccelerationStructureBuildGeometryInfoKHR*, const uint32_t*, VkAccelerationStructureBuildSizesInfoKHR* sizes) {
    sizes->accelerationStructureSize = 1024;
    sizes->buildScratchSize = 4096;
    sizes->updateScratchSize = 8192;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateAS(VkDevice, const VkAccelerationStructureCreateInfoKHR*,
    const VkAllocationCallbacks*, VkAccelerationStructureKHR* as) { *as = handle<VkAccelerationStructureKHR>(0x800); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL FakeBuildAS(VkCommandBuffer, uint32_t,
    const VkAccelerationStructureBuildGeometryInfoKHR*, const VkAccelerationStructureBuildRangeInfoKHR* const*) {
    BuildEvents.push_back("build");
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL FakeASAddress(VkDevice, const VkAccelerationStructureDeviceAddressInfoKHR*) { return 0x90000; }
struct jRHI_Vulkan {
    struct jQueue_Vulkan { VkQueue Queue = handle<VkQueue>(0x400); } Queue;
    VkDevice Device = handle<VkDevice>(0x500);
    jCommandBufferManager_Vulkan Manager;
    jSwapchain_Vulkan* Swapchain = nullptr;
    jCommandBufferManager_Vulkan* GetCommandBufferManager(ECommandBufferType) { return &Manager; }
    const jQueue_Vulkan& GetQueue(ECommandBufferType) { return Queue; }
    void RecreateSwapChain() {}
    uint32 GetCurrentFrameNumber() { return 1; }
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = FakeBufferAddress;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = FakeBuildSizes;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = FakeCreateAS;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = FakeBuildAS;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = FakeASAddress;
    void UAVBarrier(jCommandBuffer_Vulkan* command, jBuffer* buffer) {
        command->Barriers.AddUAV(buffer); command->PendingBarrier = true;
    }
    std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> QueueSubmit(std::shared_ptr<jRenderFrameContext_Vulkan>, jSemaphore*);
    std::shared_ptr<jBuffer> CreateBufferInternal(uint64, uint64, EBufferCreateFlag, EResourceLayout, const void*, uint64, jName) const;
    template<class T = jBuffer> std::shared_ptr<T> CreateRawBuffer(uint64 size, uint64 alignment, EBufferCreateFlag flags,
        EResourceLayout layout, const void* data = nullptr, uint64 bytes = 0, jName name = {}) const {
        return std::static_pointer_cast<T>(CreateBufferInternal(size, alignment, flags, layout, data, bytes, name));
    }
    std::shared_ptr<jBuffer> CreateStructuredBuffer(uint64 size, uint64 alignment, uint64,
        EBufferCreateFlag flags, EResourceLayout layout) {
        return CreateBufferInternal(size, alignment, flags, layout, nullptr, 0, {});
    }
};
jRHI_Vulkan* g_rhi_vk;
jRHI_Vulkan* g_rhi;
struct jMaterial { bool IsUseSphericalMap() const { return false; } };
struct jRenderObject {
    std::shared_ptr<jMaterial> MaterialPtr;
    std::shared_ptr<jBuffer_Vulkan> BLAS = std::make_shared<jBuffer_Vulkan>();
    struct { float m[4][4]{}; } World;
    uint32 RayTracingHitGroupIndex = 0;
    bool IsSupportRaytracing() const { return true; }
    void UpdateWorldMatrix() {}
    template<class T> T* GetBottomLevelASBuffer() { return static_cast<T*>(BLAS.get()); }
};
struct { bool EarthQuake = false; } gOptions;
constexpr uint8 HWRTDI_InstanceMask_Scene = 1, HWRTDI_InstanceMask_Sky = 2;
struct jRatracingInitializer {
    jCommandBuffer* CommandBuffer = nullptr;
    std::vector<jRenderObject*> RenderObjects;
};
struct jRaytracingScene_Vulkan {
    std::vector<jRenderObject*> InstanceList;
    std::shared_ptr<jBuffer> ScratchTLASBufferPtr, TLASBufferPtr, InstanceUploadBufferPtr;
    std::unordered_set<jRenderObject*> PendingAdd, PendingRemove, DirtyBLAS, DirtyTransform;
    bool bForceTLASRebuild = false;
    uint32 LastTLASInstanceCount = 0;
    bool ShouldUpdate() const {
        return bForceTLASRebuild || !PendingAdd.empty() || !PendingRemove.empty() || !DirtyBLAS.empty() || !DirtyTransform.empty();
    }
    void ClearDirtyFlags() {
        PendingAdd.clear(); PendingRemove.clear(); DirtyBLAS.clear(); DirtyTransform.clear(); bForceTLASRebuild = false;
    }
    std::vector<jRenderObject*> CollectBLASBuildWorkList(const jRatracingInitializer&);
    void CreateOrUpdateTLAS(const jRatracingInitializer&);
    template<class T> T* GetTLASBuffer() { return static_cast<T*>(TLASBufferPtr.get()); }
    template<class T> T* GetInstanceUploadBuffer() { return static_cast<T*>(InstanceUploadBufferPtr.get()); }
    void BumpSceneUpdateIndex() {}
};
struct jRenderFrameContext_Vulkan : std::enable_shared_from_this<jRenderFrameContext_Vulkan> {
    jCommandBuffer* CommandBuffer = nullptr;
    int FrameIndex = 0;
    jSemaphore* CurrentWaitSemaphore = nullptr;
    std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> QueueSubmitCurrentActiveCommandBuffer(jSemaphore*, bool);
};
std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> jRHI_Vulkan::QueueSubmit(
    std::shared_ptr<jRenderFrameContext_Vulkan> frame, jSemaphore* signal) {
    return Manager.QueueSubmit(static_cast<jCommandBuffer_Vulkan*>(frame->CommandBuffer), frame->CurrentWaitSemaphore, signal);
}
struct Submission {
    bool HasTimelineInfo = false;
    std::vector<VkPipelineStageFlags> WaitStages;
    std::vector<uint64> WaitValues;
    uint32 SignalCount = 0, SignalValueCount = 0;
    uint64 SignalValue = 0;
} Submitted;
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice, uint32_t, const VkFence*) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue, uint32_t count, const VkSubmitInfo* info, VkFence) {
    check(count == 1);
    Submitted = {};
    Submitted.SignalCount = info->signalSemaphoreCount;
    if (info->waitSemaphoreCount)
        Submitted.WaitStages.assign(info->pWaitDstStageMask, info->pWaitDstStageMask + info->waitSemaphoreCount);
    if (info->pNext) {
        const auto* timeline = static_cast<const VkTimelineSemaphoreSubmitInfo*>(info->pNext);
        check(timeline->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
        Submitted.HasTimelineInfo = true;
        if (timeline->waitSemaphoreValueCount)
            Submitted.WaitValues.assign(timeline->pWaitSemaphoreValues,
                timeline->pWaitSemaphoreValues + timeline->waitSemaphoreValueCount);
        Submitted.SignalValueCount = timeline->signalSemaphoreValueCount;
        if (timeline->signalSemaphoreValueCount) Submitted.SignalValue = *timeline->pSignalSemaphoreValues;
    }
    return VK_SUCCESS;
}
'''

TESTS = r'''
int main() {
    int failures = 0;
    auto test = [&](const char* name, auto checkTest) {
        try { check(checkTest()); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    jRHI_Vulkan rhi; g_rhi = g_rhi_vk = &rhi;
    jSwapchainImage_Vulkan image;
    jSwapchain_Vulkan swapchain{{&image}}; rhi.Swapchain = &swapchain;
    rhi.Manager.Replacement.Fence.Handle = handle<VkFence>(0x101);
    jCommandBuffer_Vulkan command;
    jSemaphore_Vulkan signal; signal.Type = ESemaphoreType::TIMELINE;
    auto frame = std::make_shared<jRenderFrameContext_Vulkan>();
    frame->CommandBuffer = &command;
    frame->QueueSubmitCurrentActiveCommandBuffer(&signal, true);
    test("CPU wait observes submitted fence", [&] { return command.Fence.Waits == 1 && rhi.Manager.Replacement.Fence.Waits == 0; });
    test("swapchain records submitted fence", [&] { return image.CommandBufferFence == command.Fence.Handle; });
    command.Fence.Waits = rhi.Manager.Replacement.Fence.Waits = 0;
    frame->CommandBuffer = &command;
    frame->QueueSubmitCurrentActiveCommandBuffer(&signal, false);
    test("nonblocking submission does not wait", [&] { return command.Fence.Waits == 0 && rhi.Manager.Replacement.Fence.Waits == 0; });
    jSemaphore_Vulkan wait; wait.Type = ESemaphoreType::TIMELINE; wait.Value = 7;
    rhi.Manager.QueueSubmit(&command, &wait, &signal);
    test("semaphore wait protects pre-color compute and shader accesses", [&] {
        return Submitted.WaitStages.size() == 1 && (Submitted.WaitStages[0] & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    });
    test("timeline signal value matches returned sync", [&] { return Submitted.SignalValue == signal.Value; });
    jSemaphore_Vulkan binary;
    rhi.Manager.WaitSemaphoreAcrossQueues.push_back(std::make_shared<jSyncAcrossCommandQueue_Vulkan>(ECommandBufferType::COMPUTE, &wait));
    rhi.Manager.QueueSubmit(&command, nullptr, &binary);
    test("cross-queue timeline wait with binary signal keeps timeline metadata", [&] {
        return Submitted.HasTimelineInfo && Submitted.WaitValues == std::vector<uint64>{7}
            && Submitted.SignalValueCount == Submitted.SignalCount;
    });
    test("submission without signal returns no unusable sync", [&] {
        return !rhi.Manager.QueueSubmit(&command, nullptr, nullptr);
    });
    jResourceBarrierBatcher_Vulkan barriers;
    jBuffer_Vulkan buffer;
    barriers.AddUAV(&buffer);
    test("buffer UAV dependency includes subsequent writes", [&] { return barriers.Last.dstAccessMask & VK_ACCESS_SHADER_WRITE_BIT; });
    jTexture texture;
    barriers.AddUAV(&texture);
    test("texture UAV dependency includes subsequent writes", [&] { return barriers.Last.dstAccessMask & VK_ACCESS_SHADER_WRITE_BIT; });
    buffer.AccelerationStructure = handle<VkAccelerationStructureKHR>(0x600);
    barriers.AddUAV(&buffer);
    test("AS dependency includes build write and trace/build read", [&] {
        return (barriers.Last.srcAccessMask & VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR)
            && (barriers.Last.dstAccessMask & VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR)
            && (barriers.Last.dstAccessMask & VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
    });
    rhi.CreateBufferInternal(256, 0, EBufferCreateFlag::Readback, EResourceLayout::TRANSFER_SRC, nullptr, 0, {});
    test("readback allocation is host visible and coherent", [&] {
        return !!(LastMemoryBits & EVulkanMemoryBits::HOST_VISIBLE) && !!(LastMemoryBits & EVulkanMemoryBits::HOST_COHERENT);
    });
    test("readback does not require a host-visible device-local heap", [&] { return !(LastMemoryBits & EVulkanMemoryBits::DEVICE_LOCAL); });
    rhi.CreateBufferInternal(256, 0, EBufferCreateFlag::UAV, EResourceLayout::UAV, nullptr, 0, {});
    test("GPU-only allocation remains device local", [&] { return LastMemoryBits == EVulkanMemoryBits::DEVICE_LOCAL; });
    jRenderObject object;
    jRaytracingScene_Vulkan scene;
    scene.InstanceList.push_back(&object);
    BuildEvents.clear();
    scene.CreateOrUpdateTLAS({&command});
    test("initial TLAS writes published before later trace", [&] {
        return BuildEvents == std::vector<std::string>{"build", "barrier"};
    });
    auto previousUpload = scene.InstanceUploadBufferPtr;
    BuildEvents.clear();
    scene.CreateOrUpdateTLAS({&command});
    test("TLAS update flushes dependency before build and after build", [&] {
        return BuildEvents == std::vector<std::string>{"barrier", "build", "barrier"};
    });
    test("TLAS update preserves in-flight instance upload bytes", [&] {
        return scene.InstanceUploadBufferPtr != previousUpload;
    });
    test("TLAS scratch allocation accommodates update requirement", [&] {
        return static_cast<jBuffer_Vulkan*>(scene.ScratchTLASBufferPtr.get())->AllocatedSize >= 8192;
    });
    scene.bForceTLASRebuild = true;
    scene.CreateOrUpdateTLAS({&command});
    test("TLAS consumes force and transform dirty state after building", [&] { return !scene.ShouldUpdate(); });
    jRenderObject added;
    scene.InstanceList.push_back(&added);
    auto oldTLAS = scene.TLASBufferPtr;
    scene.CreateOrUpdateTLAS({&command});
    test("instance count changes rebuild TLAS storage instead of refitting old capacity", [&] {
        return scene.TLASBufferPtr != oldTLAS;
    });
    scene.PendingRemove.insert(&added);
    scene.CreateOrUpdateTLAS({&command});
    test("pending removals shrink TLAS topology and consume dirty state", [&] {
        return scene.InstanceList.size() == 1 && scene.InstanceList[0] == &object && !scene.ShouldUpdate();
    });
    scene.PendingRemove.insert(&object);
    scene.CreateOrUpdateTLAS({&command});
    test("last instance removal retires TLAS resources", [&] {
        return scene.InstanceList.empty() && !scene.TLASBufferPtr && !scene.ShouldUpdate();
    });
#ifdef JENGINE_HAS_BLAS_WORKLIST
    jRaytracingScene_Vulkan topology;
    jRatracingInitializer initializer{&command, {&object, &object}};
    auto work = topology.CollectBLASBuildWorkList(initializer);
    test("full BLAS worklist deduplicates scene objects", [&] { return work.size() == 1; });
    topology.InstanceList.push_back(&object);
    bool stableTopology = true;
    for (int frameIndex = 0; frameIndex < 60; ++frameIndex) {
        topology.bForceTLASRebuild = true;
        topology.DirtyTransform.insert(&object);
        stableTopology &= topology.CollectBLASBuildWorkList(initializer).empty();
        topology.CreateOrUpdateTLAS(initializer);
        stableTopology &= topology.InstanceList.size() == 1 && !topology.ShouldUpdate();
    }
    test("repeated forced transform updates retain bounded topology and skip BLAS rebuilds", [&] { return stableTopology; });
    topology.PendingAdd.insert(&added);
    topology.DirtyBLAS.insert(&added);
    work = topology.CollectBLASBuildWorkList(initializer);
    test("dirty and added BLAS queues deduplicate the same object", [&] { return work.size() == 1 && work[0] == &added; });
#endif
#ifdef JENGINE_HAS_DESCRIPTOR_BUDGET_ALLOCATOR
    jDescriptorPool_Vulkan pool;
    const std::vector<VkDescriptorPoolSize> smallRequirements{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 52}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1; poolInfo.poolSizeCount = 1; poolInfo.pPoolSizes = smallRequirements.data();
    vkCreateDescriptorPool(rhi.Device, &poolInfo, nullptr, &pool.DescriptorPool);
    pool.RemainingDescriptors[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] = 52;
    const auto smallLayout = handle<VkDescriptorSetLayout>(0x3000);
    DriverLayoutRequirements[smallLayout] = smallRequirements;
    const auto smallSet = pool.AllocateDescriptorSet(smallLayout, smallRequirements);
    test("descriptor allocation consumes the actual primary type budget", [&] {
        return smallSet && pool.RemainingDescriptors[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] == 0 && pool.OverflowDescriptorPools.empty();
    });
    const auto largeLayout = handle<VkDescriptorSetLayout>(0x3001);
    const std::vector<VkDescriptorPoolSize> largeRequirements{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2912}};
    DriverLayoutRequirements[largeLayout] = largeRequirements;
    const auto largeSet = pool.AllocateDescriptorSet(largeLayout, largeRequirements);
    test("oversized bindless layout gets an exact-sized overflow pool before allocation", [&] {
        return largeSet && pool.OverflowDescriptorPools.size() == 1 && !UnexpectedPoolOverrun
            && DriverPools[pool.OverflowDescriptorPools[0]].Sets == 1
            && DriverPools[pool.OverflowDescriptorPools[0]].Remaining[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] == 0;
    });
    pool.PendingDescriptorSets = pool.AllocatedDescriptorSets;
    const auto poolCount = DriverPools.size();
    test("overflow descriptor sets are reused by the existing cache", [&] {
        return pool.AllocateDescriptorSet(largeLayout, largeRequirements) == largeSet && DriverPools.size() == poolCount;
    });
    const auto failingLayout = handle<VkDescriptorSetLayout>(0x3002);
    DriverLayoutRequirements[failingLayout] = largeRequirements;
    FailNextDescriptorAllocation = true;
    test("failed overflow set allocation releases its newly created pool", [&] {
        return !pool.AllocateDescriptorSet(failingLayout, largeRequirements) && DriverPools.size() == poolCount;
    });
    pool.Release();
    test("descriptor pool release destroys primary and overflow pools and cached sets", [&] {
        return DriverPools.empty() && pool.AllocatedDescriptorSets.empty() && pool.PendingDescriptorSets.empty();
    });
#endif
    std::cout << "Vulkan synchronization checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--source-ref', help='Read method bodies from this Git ref for a negative control')
    args = parser.parse_args()
    root = args.source.resolve()
    bodies = []
    for path, signatures in (
        ('jCommandBufferManager_Vulkan.cpp', (
            'jSyncAcrossCommandQueue_Vulkan::jSyncAcrossCommandQueue_Vulkan(',
            'void jCommandBufferManager_Vulkan::GetWaitSemaphoreAndValueThenClear(',
            'std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> jCommandBufferManager_Vulkan::QueueSubmit(')),
        ('jRenderFrameContext_Vulkan.cpp', (
            'std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> jRenderFrameContext_Vulkan::QueueSubmitCurrentActiveCommandBuffer(',)),
        ('jResourceBarrierBatcher_Vulkan.cpp', (
            'void jResourceBarrierBatcher_Vulkan::AddUAV(jBuffer*',
            'void jResourceBarrierBatcher_Vulkan::AddUAV(jTexture*')),
        ('jRHI_Vulkan.cpp', ('std::shared_ptr<jBuffer> jRHI_Vulkan::CreateBufferInternal(',)),
        ('jRaytracingScene_Vulkan.cpp', (
            'std::vector<jRenderObject*> jRaytracingScene_Vulkan::CollectBLASBuildWorkList(',
            'void jRaytracingScene_Vulkan::CreateOrUpdateTLAS(')),
        ('jDescriptorPool_Vulkan.cpp', (
            'std::shared_ptr<jShaderBindingInstance> jDescriptorPool_Vulkan::AllocateDescriptorSet(',
            'void jDescriptorPool_Vulkan::Release(')),
    ):
        relative = Path('jEngine/RHI/Vulkan') / path
        source = (subprocess.check_output(['git', 'show', f'{args.source_ref}:{relative.as_posix()}'], cwd=root).decode('utf-8-sig')
                  if args.source_ref else (root / relative).read_text(encoding='utf-8-sig'))
        if path == 'jDescriptorPool_Vulkan.cpp':
            allocation = method(source, signatures[0])
            if 'InRequirements' not in allocation and args.source_ref: continue
            bodies.append('#define JENGINE_HAS_DESCRIPTOR_BUDGET_ALLOCATOR\n')
        for signature in signatures:
            if 'CollectBLASBuildWorkList(' in signature:
                if signature not in source and args.source_ref: continue
                bodies.append('#define JENGINE_HAS_BLAS_WORKLIST\n')
            bodies.append(method(source, signature))
    # These methods retain their production bodies; only their surrounding
    # engine/driver dependencies are replaced by the deterministic stubs above.
    with tempfile.TemporaryDirectory(prefix='jengine-vulkan-sync-') as directory:
        directory = Path(directory)
        cpp = directory / 'check.cpp'
        cpp.write_text(STUBS + '\n'.join(bodies) + TESTS)
        executable = directory / ('check.exe' if __import__('os').name == 'nt' else 'check')
        include = root / 'jEngine/External/vulkan/1.3.239.0/Include'
        compiler = Path(args.compiler).name.lower()
        if compiler in ('cl', 'cl.exe'):
            command = [args.compiler, '/nologo', '/std:c++20', '/EHsc', f'/I{include}', str(cpp), f'/Fe:{executable}']
        else:
            command = [args.compiler, '-std=c++20', '-I', str(include), str(cpp), '-o', str(executable)]
        subprocess.run(command, check=True, cwd=directory)
        raise SystemExit(subprocess.run([str(executable)], cwd=directory).returncode)


if __name__ == '__main__':
    main()
