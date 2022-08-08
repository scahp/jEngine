#include "pch.h"
#include "jRenderTarget.h"
#include "jRenderTargetPool.h"

void jRenderTarget::Return()
{
    if (bCreatedFromRenderTargetPool)
    {
        jRenderTargetPool::ReturnRenderTarget(this);
    }
}
