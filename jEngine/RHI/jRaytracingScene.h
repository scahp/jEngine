#pragma once

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
    virtual void CreateOrUpdateBLAS(const jRatracingInitializer& InInitializer) = 0;
    virtual void CreateOrUpdateTLAS(const jRatracingInitializer& InInitializer) = 0;
    virtual bool IsValid() const { return TLASBuffer != nullptr; }

    std::vector<jRenderObject*> InstanceList;
    jBuffer* TLASBuffer = nullptr;
    jBuffer* ScratchTLASBuffer = nullptr;
    jBuffer* InstanceUploadBuffer = nullptr;
};

