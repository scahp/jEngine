#include "pch.h"
#include "jRenderTarget.h"
#include "jRenderTargetPool.h"

jRenderTarget::~jRenderTarget()
{
    Return();
	if (!RelatedRenderPassHashes.empty())
	{
		g_rhi->RemoveRenderPassByHash(RelatedRenderPassHashes);
		RelatedRenderPassHashes.clear();
	}
}

void jRenderTarget::Return()
{
    if (bCreatedFromRenderTargetPool)
    {
        jRenderTargetPool::ReturnRenderTarget(this);
		if (!RelatedRenderPassHashes.empty())
		{
			g_rhi->RemoveRenderPassByHash(RelatedRenderPassHashes);
			RelatedRenderPassHashes.clear();
		}
    }
}
