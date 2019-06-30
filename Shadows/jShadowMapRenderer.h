#pragma once

#include "jRenderer.h"
#include "jRHI.h"
#include "jShadowTypes.h"

class jFullscreenQuadPrimitive;
struct jShader;

class jShadowMapRenderer : public jRenderer
{
public:
	jShadowMapRenderer() {}
	virtual ~jShadowMapRenderer() {}

	virtual void ShadowPrePass(jCamera* camera) override;
	virtual void RenderPass(jCamera* camera) override;
	virtual void UIPass(jCamera* camera) override;

	jFullscreenQuadPrimitive* FullscreenQuadPrimitive = nullptr;
	
	jShader* BlurShader = nullptr;
	jShader* BlurOmniShader = nullptr;

	jShader* PCF_Poisson_Shader = nullptr;
	jShader* PCF_Shader = nullptr;
	jShader* PCSS_Poisson_Shader = nullptr;
	jShader* PCSS_Shader = nullptr;
	jShader* VSM_Shader = nullptr;
	jShader* ESM_Shader = nullptr;
	jShader* EVSM_Shader = nullptr;
	jShader* SSM_Shader = nullptr;
	jShader* Hair_Shader = nullptr;

	jShaderInfo Blur;
	jShaderInfo BlurOmni;

	jShaderInfo PCF_Poisson;
	jShaderInfo PCF;
	jShaderInfo PCSS_Poisson;
	jShaderInfo PCSS;
	jShaderInfo VSM;
	jShaderInfo ESM;
	jShaderInfo EVSM;
	jShaderInfo SSM;
	jShaderInfo Hair;

	//////////////////////////////////////////////////////////////////////////
	// Shadow Gen
	jShaderInfo ShadowGen_VSM;
	jShaderInfo ShadowGen_ESM;
	jShaderInfo ShadowGen_EVSM;
	jShaderInfo ShadowGen_SSM;

	jShader* ShadowGen_VSM_Shader = nullptr;
	jShader* ShadowGen_ESM_Shader = nullptr;
	jShader* ShadowGen_EVSM_Shader = nullptr;
	jShader* ShadowGen_SSM_Shader = nullptr;

	jShaderInfo ShadowGen_Omni_ESM;
	jShaderInfo ShadowGen_Omni_EVSM;
	jShaderInfo ShadowGen_Omni_SSM;

	jShader* ShadowGen_Omni_VSM_Shader = nullptr;
	jShader* ShadowGen_Omni_ESM_Shader = nullptr;
	jShader* ShadowGen_Omni_EVSM_Shader = nullptr;
	jShader* ShadowGen_Omni_SSM_Shader = nullptr;

	//////////////////////////////////////////////////////////////////////////

	jShaderInfo UIShadowInfo;
	jShader* UIShader = nullptr;

	class jObject* ShadowMapDebugQuad = nullptr;
	
	virtual void Setup() override;
	virtual void UpdateSettings() override;

	EShadowMapType ShadowMapType = EShadowMapType::MAX;
	bool UsePoissonSample = false;

	virtual void DebugRenderPass(jCamera* camera) override;

};

