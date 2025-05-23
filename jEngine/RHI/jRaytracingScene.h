﻿#pragma once

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
    virtual bool ShouldUpdate() const { return false; }
    virtual void Clear()
    {
        InstanceList.clear();
        TLASBufferPtr.reset();
        ScratchTLASBufferPtr.reset();
        InstanceUploadBufferPtr.reset();
        RaytracingOutputPtr.reset();
    }

    template <typename T> T* GetTLASBuffer() const { return (T*)TLASBufferPtr.get(); }
    template <typename T> T* GetScratchTLASBuffer() const { return (T*)ScratchTLASBufferPtr.get(); }
    template <typename T> T* GetInstanceUploadBuffer() const { return (T*)InstanceUploadBufferPtr.get(); }

    std::vector<jRenderObject*> InstanceList;
    std::shared_ptr<jBuffer> TLASBufferPtr;
    std::shared_ptr<jBuffer> ScratchTLASBufferPtr;
    std::shared_ptr<jBuffer> InstanceUploadBufferPtr;
    std::shared_ptr<jTexture> RaytracingOutputPtr;
};

