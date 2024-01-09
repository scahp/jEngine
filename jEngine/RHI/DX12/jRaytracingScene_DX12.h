#pragma once

#include "../jRaytracingScene.h"

struct jBuffer;
class jRenderObject;

class jRaytracingScene_DX12 : public jRaytracingScene
{
public:
    virtual void CreateOrUpdateBLAS(const jRatracingInitializer& InInitializer) override;
    virtual void CreateOrUpdateTLAS(const jRatracingInitializer& InInitializer) override;
};