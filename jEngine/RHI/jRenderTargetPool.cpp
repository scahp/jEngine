﻿#include "pch.h"
#include "jRenderTargetPool.h"
#include "jRHI.h"

std::shared_ptr<jRenderTarget> g_EyeAdaptationARTPtr;
std::shared_ptr<jRenderTarget> g_EyeAdaptationBRTPtr;

std::map<size_t, std::list<jRenderTargetPool::jRenderTargetPoolResource> > jRenderTargetPool::RenderTargetResourceMap;
std::map<jRenderTarget*, size_t> jRenderTargetPool::RenderTargetHashVariableMap;
std::vector<std::shared_ptr<jRenderTarget>> jRenderTargetPool::OneFrameRenderTargets;
bool jRenderTargetPool::PreventReturnRenderTarget = false;

struct jTexture* jRenderTargetPool::GetNullTexture(ETextureType type)
{
	static std::shared_ptr<jRenderTarget> RTPtr = jRenderTargetPool::GetRenderTarget({ type, ETextureFormat::RGBA8, 2, 2, 1 });
	return RTPtr->GetTexture();
}

jRenderTargetPool::jRenderTargetPool()
{
}


jRenderTargetPool::~jRenderTargetPool()
{
}

std::shared_ptr<jRenderTarget> jRenderTargetPool::GetRenderTarget(const jRenderTargetInfo& info)
{
	auto hash = info.GetHash();

	auto it_find = RenderTargetResourceMap.find(hash);
	if (RenderTargetResourceMap.end() != it_find)
	{
		auto& renderTargets = it_find->second;
		for (auto& iter : renderTargets)
		{
			if (!iter.IsUsing)
			{
				iter.IsUsing = true;
				return iter.RenderTargetPtr;
			}
		}
	}

	auto renderTargetPtr = g_rhi->CreateRenderTarget(info);
	if (renderTargetPtr)
	{
		renderTargetPtr->bCreatedFromRenderTargetPool = true;
		RenderTargetResourceMap[hash].push_back({ true, renderTargetPtr });
		RenderTargetHashVariableMap[renderTargetPtr.get()] = hash;
	}
	
	return renderTargetPtr;
}

std::shared_ptr<jRenderTarget> jRenderTargetPool::GetRenderTargetForOneFrame(const jRenderTargetInfo& info)
{
	auto NewRT = GetRenderTarget(info);
	check(NewRT->bCreatedFromRenderTargetPool);
	NewRT->bOneFrameRenderTarget = true;
	OneFrameRenderTargets.push_back(NewRT);
	return NewRT;
}

void jRenderTargetPool::ReturnRenderTarget(jRenderTarget* renderTarget)
{
	if (PreventReturnRenderTarget)
		return;

	auto it_find = RenderTargetHashVariableMap.find(renderTarget);
	if (RenderTargetHashVariableMap.end() == it_find)
	{
		check(0);
		return;
	}

	const size_t hash = it_find->second;
	for (auto& iter : RenderTargetResourceMap[hash])
	{
		if (renderTarget == iter.RenderTargetPtr.get())
		{
			iter.IsUsing = false;
			return;
		}
	}
	check(0);
}
