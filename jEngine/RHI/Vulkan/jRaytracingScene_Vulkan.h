#pragma once

#include "../jRaytracingScene.h"

struct jBuffer;
class jRenderObject;

class jRaytracingScene_Vulkan : public jRaytracingScene
{
public:
    virtual void CreateOrUpdateBLAS(const jRatracingInitializer& InInitializer) override;
    virtual void CreateOrUpdateTLAS(const jRatracingInitializer& InInitializer) override;

private:
    std::vector<jRenderObject*> CollectBLASBuildWorkList(const jRatracingInitializer& InInitializer);
    uint32 LastTLASInstanceCount = 0;
};
