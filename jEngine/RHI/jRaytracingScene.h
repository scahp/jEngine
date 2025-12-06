#pragma once

#include <vector>
#include <unordered_set>

class jCommandBuffer;
class jRenderObject;
struct jBuffer;

struct jRatracingInitializer
{
    std::vector<jRenderObject*> RenderObjects;
    jCommandBuffer* CommandBuffer = nullptr;
};

class jRaytracingScene
{
public:
    virtual ~jRaytracingScene();
    virtual void CreateOrUpdateBLAS(const jRatracingInitializer& InInitializer) = 0;
    virtual void CreateOrUpdateTLAS(const jRatracingInitializer& InInitializer) = 0;
    virtual bool IsValid() const { return TLASBufferPtr != nullptr; }
    virtual bool ShouldUpdate() const { return bForceTLASRebuild || !PendingAdd.empty() || !PendingRemove.empty() || !DirtyBLAS.empty() || !DirtyTransform.empty(); }

    virtual void MarkAdd(jRenderObject* InObject) { if (InObject) PendingAdd.insert(InObject); }
    virtual void MarkRemove(jRenderObject* InObject) { if (InObject) PendingRemove.insert(InObject); }
    virtual void MarkBLASDirty(jRenderObject* InObject) { if (InObject) { DirtyBLAS.insert(InObject); bForceTLASRebuild = true; } }
    virtual void MarkTransformDirty(jRenderObject* InObject) { if (InObject) { DirtyTransform.insert(InObject); bForceTLASRebuild = true; } }
    void ForceRebuildTLAS() { bForceTLASRebuild = true; }
    void ClearDirtyFlags()
    {
        PendingAdd.clear();
        PendingRemove.clear();
        DirtyBLAS.clear();
        DirtyTransform.clear();
        bForceTLASRebuild = false;
    }

    uint64 GetSceneUpdateIndex() const { return SceneUpdateIndex; }
    void BumpSceneUpdateIndex() { ++SceneUpdateIndex; }

    virtual void Clear()
    {
        InstanceList.clear();
        TLASBufferPtr.reset();
        ScratchTLASBufferPtr.reset();
        InstanceUploadBufferPtr.reset();
        RaytracingOutputPtr.reset();
        ClearDirtyFlags();
        SceneUpdateIndex = 0;
    }

    template <typename T> T* GetTLASBuffer() const { return (T*)TLASBufferPtr.get(); }
    template <typename T> T* GetScratchTLASBuffer() const { return (T*)ScratchTLASBufferPtr.get(); }
    template <typename T> T* GetInstanceUploadBuffer() const { return (T*)InstanceUploadBufferPtr.get(); }

    std::vector<jRenderObject*> InstanceList;
    std::shared_ptr<jBuffer> TLASBufferPtr;
    std::shared_ptr<jBuffer> ScratchTLASBufferPtr;
    std::shared_ptr<jBuffer> InstanceUploadBufferPtr;
    std::shared_ptr<jTexture> RaytracingOutputPtr;

    std::unordered_set<jRenderObject*> PendingAdd;
    std::unordered_set<jRenderObject*> PendingRemove;
    std::unordered_set<jRenderObject*> DirtyBLAS;
    std::unordered_set<jRenderObject*> DirtyTransform;
    bool bForceTLASRebuild = false;
    uint64 SceneUpdateIndex = 0;
};
