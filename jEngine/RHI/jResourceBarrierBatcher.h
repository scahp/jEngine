#pragma once
#include "jRHIType.h"

struct jBuffer;
struct jTexture;
class jCommandBuffer;

class jResourceBarrierBatcher
{
public:
    virtual ~jResourceBarrierBatcher() {}

    virtual void AddUAV(jBuffer* InBuffer) = 0;
    virtual void AddUAV(jTexture* InBuffer) = 0;

    virtual void AddTransition(jBuffer* InBuffer, EResourceLayout InNewLayout) = 0;
    virtual void AddTransition(jTexture* InTexture, EResourceLayout InNewLayout) = 0;

    virtual void Flush(jCommandBuffer* InCommandBuffer) = 0;
};
