#pragma once

#include "jRenderer.h"

class jRenderer_PathTracing : public IRenderer
{
public:
	using IRenderer::IRenderer;

	virtual void Setup() override;
	virtual void Render() override;
	virtual void PathTracing();
};