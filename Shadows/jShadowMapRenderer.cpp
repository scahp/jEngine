#include "pch.h"
#include "jShadowMapRenderer.h"
#include "jRHI.h"
#include "jObject.h"
#include "jLight.h"
#include "jCamera.h"
#include "jGame.h"
#include "jRenderTargetPool.h"
#include "jPrimitiveUtil.h"
#include "jRenderObject.h"
#include "jShadowAppProperties.h"
#include "jAppSettings.h"

void jShadowMapRenderer::ShadowPrePass(const jCamera* camera)
{
	jShader* ShadowGenShader = nullptr;
	switch (ShadowMapType)
	{
	case EShadowMapType::VSM:
		ShadowGenShader = ShadowGen_VSM_Shader;
		break;
	case EShadowMapType::ESM:
		ShadowGenShader = ShadowGen_ESM_Shader;
		break;
	case EShadowMapType::EVSM:
		ShadowGenShader = ShadowGen_EVSM_Shader;
		break;
	default:
		ShadowGenShader = ShadowGen_SSM_Shader;
		break;
	}

	jShader* ShadowGenOmniShader = nullptr;
	switch (ShadowMapType)
	{
	case EShadowMapType::ESM:
		ShadowGenOmniShader = ShadowGen_Omni_ESM_Shader;
		break;
	case EShadowMapType::EVSM:
		ShadowGenOmniShader = ShadowGen_Omni_EVSM_Shader;
		break;
	default:
		ShadowGenOmniShader = ShadowGen_Omni_SSM_Shader;
		break;
	}

	g_rhi->EnableDepthTest(true);
	g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);
	
	// 1. ShadowMap pass
	for (auto& light : camera->LightList)
	{
		switch (light->Type)
		{
		case ELightType::DIRECTIONAL:
		{
			// 1.1 Directional Light ShadowMap Generation
			auto directionalLight = static_cast<jDirectionalLight*>(light);
			g_rhi->SetRenderTarget(directionalLight->ShadowMapData->ShadowMapRenderTarget.get());

			g_rhi->EnableDepthTest(true);
			g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);

			float maxDist = 0.0f;
			float maxDistSquare = 0.0f;
			switch(ShadowMapType)
			{
			case EShadowMapType::VSM:
			case EShadowMapType::ESM:
			case EShadowMapType::EVSM:
				maxDist = FLT_MAX;
				maxDistSquare = FLT_MAX;
				//maxDist = expf(maxDist);
				//maxDistSquare = expf(maxDistSquare);
				break;
			default:
				break;
			}

			g_rhi->SetClearColor(maxDist, maxDistSquare, 0.0f, 1.0f);
			g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

			const auto& shadowCasterObjects = jObject::GetShadowCasterObject();
			for (auto& iter : shadowCasterObjects)
				iter->Draw(directionalLight->ShadowMapData->ShadowMapCamera, ShadowGenShader, { light });
			break;
		}
		case ELightType::POINT:
		{
			// 1.2 Point Light ShadowMap Generation
			auto pointLight = static_cast<jPointLight*>(light);
			for (int32 i = 0; i < 6; ++i)
			{
				pointLight->ShadowMapData->ShadowMapCamera[i]->UpdateCamera();

				g_rhi->SetRenderTarget(pointLight->ShadowMapData->ShadowMapRenderTarget.get(), i);

				g_rhi->EnableDepthTest(true);
				g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);
	
				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

				const auto& shadowCasterObjects = jObject::GetShadowCasterObject();
				for (auto& iter : shadowCasterObjects)
					iter->Draw(pointLight->ShadowMapData->ShadowMapCamera[i], ShadowGenOmniShader, { light });
			}
			break;
		}
		case ELightType::SPOT:
		{
			// 1.3 Spot Light ShadowMap Generation
			auto spotLight = static_cast<jSpotLight*>(light);
			for (int32 i = 0; i < 6; ++i)
			{
				spotLight->ShadowMapData->ShadowMapCamera[i]->UpdateCamera();

				g_rhi->SetRenderTarget(spotLight->ShadowMapData->ShadowMapRenderTarget.get(), i);

				g_rhi->EnableDepthTest(true);
				g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);

				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));
				
				const auto& shadowCasterObjects = jObject::GetShadowCasterObject();
				for (auto& iter : shadowCasterObjects)
					iter->Draw(spotLight->ShadowMapData->ShadowMapCamera[i], ShadowGenOmniShader, { light });
			}
			break;
		}
		default:
			break;
		}
		g_rhi->SetRenderTarget(nullptr);

		// Blur
		switch (ShadowMapType)
		{
		case EShadowMapType::VSM:
		case EShadowMapType::ESM:
		case EShadowMapType::EVSM:
		{
			auto vsmBlurRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, SM_WIDTH, SM_HEIGHT, 1 });

			////////////////////
			// Directional Shadow
			if (light->Type == ELightType::DIRECTIONAL)
			{
				auto direcitonalLight = static_cast<jDirectionalLight*>(light);
				FullscreenQuadPrimitive->MaxDist = direcitonalLight->ShadowMapData->ShadowMapCamera->Far * 2.0f;
				FullscreenQuadPrimitive->RenderObject->tex_object_array = nullptr;

				// vertical
				g_rhi->SetRenderTarget(vsmBlurRenderTarget.get());
				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ERenderBufferType::COLOR, ERenderBufferType::DEPTH}));
				FullscreenQuadPrimitive->IsVertical = true;
				FullscreenQuadPrimitive->RenderObject->tex_object = direcitonalLight->ShadowMapData->ShadowMapRenderTarget->GetTexture();
				FullscreenQuadPrimitive->Draw(camera, BlurShader, { light });
				// 원본 텍스쳐 -> 임시 텍스쳐

				// horizontal
				g_rhi->SetRenderTarget(direcitonalLight->ShadowMapData->ShadowMapRenderTarget.get());
				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));
				FullscreenQuadPrimitive->IsVertical = false;
				FullscreenQuadPrimitive->RenderObject->tex_object = vsmBlurRenderTarget->GetTexture();
				FullscreenQuadPrimitive->Draw(camera, BlurShader, { light });
				// 임시 텍스쳐 -> 원본 텍스쳐
			}
			//////////////////////

			jRenderTargetPool::ReturnRenderTarget(vsmBlurRenderTarget.get());

			auto vsmTexArrayBlurRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D_ARRAY_OMNISHADOW, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, SM_WIDTH, SM_HEIGHT, 6 });
			const std::initializer_list<EDrawBufferType> drawBufferArray = { EDrawBufferType::COLOR_ATTACHMENT0, EDrawBufferType::COLOR_ATTACHMENT1, EDrawBufferType::COLOR_ATTACHMENT2
				, EDrawBufferType::COLOR_ATTACHMENT3, EDrawBufferType::COLOR_ATTACHMENT4, EDrawBufferType::COLOR_ATTACHMENT5 };

			//////////////////////
			// Point Light OmniDirectional Shadow
			if (light->Type == ELightType::POINT)
			{
				auto pointLight = static_cast<jPointLight*>(light);
				FullscreenQuadPrimitive->RenderObject->tex_object = nullptr;

				// vertical
				g_rhi->SetRenderTarget(vsmTexArrayBlurRenderTarget.get(), 0, true);
				g_rhi->SetDrawBuffers(drawBufferArray);
				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));
				FullscreenQuadPrimitive->IsVertical = true;
				FullscreenQuadPrimitive->RenderObject->tex_object_array = pointLight->ShadowMapData->ShadowMapRenderTarget->GetTexture();
				FullscreenQuadPrimitive->Draw(camera, BlurOmniShader, {});

				// horizontal
				g_rhi->SetRenderTarget(pointLight->ShadowMapData->ShadowMapRenderTarget.get(), 0, true);
				g_rhi->SetDrawBuffers(drawBufferArray);
				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));
				FullscreenQuadPrimitive->IsVertical = false;
				FullscreenQuadPrimitive->RenderObject->tex_object_array = vsmTexArrayBlurRenderTarget->GetTexture();
				FullscreenQuadPrimitive->Draw(camera, BlurOmniShader, {});
			}
			////////////////////

			//////////////////////
			// Spot Light OmniDirectional Shadow
			if (light->Type == ELightType::SPOT)
			{
				auto spotLight = static_cast<jSpotLight*>(light);
				FullscreenQuadPrimitive->RenderObject->tex_object = nullptr;

				// vertical
				g_rhi->SetRenderTarget(vsmTexArrayBlurRenderTarget.get(), 0, true);
				g_rhi->SetDrawBuffers(drawBufferArray);
				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));
				FullscreenQuadPrimitive->IsVertical = true;
				FullscreenQuadPrimitive->RenderObject->tex_object_array = spotLight->ShadowMapData->ShadowMapRenderTarget->GetTexture();
				FullscreenQuadPrimitive->Draw(camera, BlurOmniShader, {});

				// horizontal
				g_rhi->SetRenderTarget(spotLight->ShadowMapData->ShadowMapRenderTarget.get(), 0, true);
				g_rhi->SetDrawBuffers(drawBufferArray);
				g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));
				FullscreenQuadPrimitive->IsVertical = false;
				FullscreenQuadPrimitive->RenderObject->tex_object_array = vsmTexArrayBlurRenderTarget->GetTexture();
				FullscreenQuadPrimitive->Draw(camera, BlurOmniShader, {});
			}
			////////////////////

			jRenderTargetPool::ReturnRenderTarget(vsmTexArrayBlurRenderTarget.get());

			break;
		}
		default:
			break;
		}
	}

	g_rhi->SetRenderTarget(nullptr);
}

void jShadowMapRenderer::RenderPass(const jCamera* camera)
{
	jShader* baseShader = nullptr;
	switch (ShadowMapType)
	{
	//case EShadowMapType::PCF:     // PCF
	//	if (UsePoissonSample)
	//		baseShader = PCF_Poisson_Shader;
	//	else
	//		baseShader = PCF_Shader;
	//	break;
	//case EShadowMapType::PCSS:     // PCSS
	//	if (UsePoissonSample)
	//		baseShader = PCSS_Poisson_Shader;
	//	else
	//		baseShader = PCSS_Shader;
	//	break;
	case EShadowMapType::VSM:     // VSM
		baseShader = VSM_Shader;;
		break;
	case EShadowMapType::ESM:     // ESM
		baseShader = ESM_Shader;
		break;
	case EShadowMapType::EVSM:     // EVSM
		baseShader = EVSM_Shader;
		break;
	default:
		baseShader = SSM_Shader;
		break;
	}

	g_rhi->SetRenderTarget(nullptr);
	g_rhi->EnableDepthTest(true);
	g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);

	g_rhi->EnableBlend(true);

	g_rhi->SetClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	// 2. Light pass
	g_rhi->SetBlendFunc(EBlendSrc::ONE, EBlendDest::ZERO);
	std::list<const jLight*> lights;
	lights.insert(lights.end(), camera->LightList.begin(), camera->LightList.end());
	const auto& staticObjects = jObject::GetStaticObject();
	for (auto& iter : staticObjects)
	{
		//if (iter->RenderObject->tex_object2/* || obj.material*/)
		//{
		//	// 쉐이더 파일 이름 채워야함.
		//	jShaderInfo shaderInfo;
		//	switch (ShadowMapType)
		//	{
		//	case EShadowMapType::PCF:     // PCF
		//		shaderInfo = (UsePoissonSample ? PCF_Poisson : PCF);
		//		break;
		//	case EShadowMapType::PCSS:     // PCSS
		//		shaderInfo = (UsePoissonSample ? PCSS_Poisson : PCSS);
		//		break;
		//	case EShadowMapType::VSM:     // VSM
		//		shaderInfo = VSM;
		//		break;
		//	case EShadowMapType::ESM:     // ESM
		//		shaderInfo = ESM;
		//		break;
		//	case EShadowMapType::EVSM:     // EVSM
		//		shaderInfo = EVSM;
		//		break;
		//	default:
		//		shaderInfo = SSM;
		//		break;
		//	}
		//	if (iter->RenderObject->tex_object2)
		//	{
		//		shaderInfo.vsPreProcessor += "\n#define USE_TEXTURE 1";
		//		shaderInfo.fsPreProcessor += "\n#define USE_TEXTURE 1";
		//	}
		//	//if (obj.material)
		//	//{
		//	//	shaderInfo.vsPreprocessor += "\n#define USE_MATERIAL 1";
		//	//	shaderInfo.fsPreprocessor += "\n#define USE_MATERIAL 1";
		//	//}

		//	auto shader = jShader::CreateShader(shaderInfo);
		//	iter->Draw(camera, shader);
		//}
		//else
		{
			iter->Draw(camera, baseShader, lights);
		}
	}

	for (auto& iter : g_HairObjectArray)
	{
		iter->Draw(camera, Hair_Shader, lights);
	}

//	// 3. Transparent object render
//	gl.enable(gl.BLEND);
//	gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
//	gl.depthFunc(gl.LEQUAL);
//	gl.depthMask(true);
//	gl.colorMask(true, true, true, true);
//	gl.disable(gl.STENCIL_TEST);
//	drawStaticTransparentObjects(camera, defaultPipeLineHashCode, -1);
//
//	if (CubeTest && camera.lights.pointLights.length)
//	{
//		//CubeTest.textureArray = vsmTexArrayBlurFrameBuffer.tbo;
//		CubeTest.textureArray = camera.lights.pointLights[0].getShadowMap();
//		CubeTest.drawFunc(camera, null, camera.lights.pointLights[0].index);
//	}
//
//	gl.disable(gl.BLEND);
}

void jShadowMapRenderer::UIPass(const jCamera* camera)
{
	g_rhi->EnableDepthTest(false);

	if (jShadowAppSettingProperties::GetInstance().ShowDirectionalLightMap)
	{
		auto directionalLight = static_cast<jDirectionalLight*>(camera->GetLight(ELightType::DIRECTIONAL));
		if (directionalLight)
		{
			if (!ShadowMapDebugQuad)
				ShadowMapDebugQuad = jPrimitiveUtil::CreateUIQuad(Vector2(10.0f, 10.0f), Vector2(120.0f, 120.0f), directionalLight->ShadowMapData->ShadowMapRenderTarget->GetTexture());
			ShadowMapDebugQuad->Draw(camera, UIShader, { directionalLight });
		}
	}
}

void jShadowMapRenderer::Setup()
{
	__super::Setup();
	FullscreenQuadPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

	// Blur
	Blur.name = "Blur";
	Blur.vs = "shaders/fullscreen/vs_blur.glsl";
	Blur.fs = "shaders/fullscreen/fs_blur.glsl";
	BlurShader = jShader::CreateShader(Blur);

	BlurOmni.name = "BlurOmni";
	BlurOmni.vs = "shaders/fullscreen/vs_omnidirectional_blur.glsl";
	BlurOmni.fs = "shaders/fullscreen/fs_omnidirectional_blur.glsl";
	BlurOmniShader = jShader::CreateShader(BlurOmni);

	// Shadow
	PCF_Poisson.name = "PCF_Poisson";
	PCF_Poisson.vs = "shaders/shadowmap/vs.glsl";
	PCF_Poisson.fs = "shaders/shadowmap/fs.glsl";
	PCF_Poisson.fsPreProcessor = "#define USE_PCF 1\r\n#define USE_POISSON_SAMPLE 1";
	PCF_Poisson_Shader = jShader::CreateShader(PCF_Poisson);

	PCF.name = "PCF";
	PCF.vs = "shaders/shadowmap/vs.glsl";
	PCF.fs = "shaders/shadowmap/fs.glsl";
	PCF.fsPreProcessor = "#define USE_PCF 1\r\n#define USE_TEXTURE";
	PCF_Shader = jShader::CreateShader(PCF);

	PCSS_Poisson.name = "PCSS_Poisson";
	PCSS_Poisson.vs = "shaders/shadowmap/vs.glsl";
	PCSS_Poisson.fs = "shaders/shadowmap/fs.glsl";
	PCSS_Poisson.fsPreProcessor = "#define USE_PCSS 1\r\n#define USE_POISSON_SAMPLE 1\r\n#define USE_TEXTURE";
	PCSS_Poisson_Shader = jShader::CreateShader(PCSS_Poisson);

	PCSS.name = "PCSS";
	PCSS.vs = "shaders/shadowmap/vs.glsl";
	PCSS.fs = "shaders/shadowmap/fs.glsl";
	PCSS.fsPreProcessor = "#define USE_PCSS 1\r\n#define USE_TEXTURE";
	PCSS_Shader = jShader::CreateShader(PCSS);

	VSM.name = "VSM";
	VSM.vs = "shaders/shadowmap/vs.glsl";
	VSM.fs = "shaders/shadowmap/fs.glsl";
	VSM.fsPreProcessor = "#define USE_VSM 1\r\n#define USE_TEXTURE";
	VSM_Shader = jShader::CreateShader(VSM);

	ESM.name = "ESM";
	ESM.vs = "shaders/shadowmap/vs.glsl";
	ESM.fs = "shaders/shadowmap/fs.glsl";
	ESM.fsPreProcessor = "#define USE_ESM 1\r\n#define USE_TEXTURE";
	ESM_Shader = jShader::CreateShader(ESM);

	EVSM.name = "EVSM";
	EVSM.vs = "shaders/shadowmap/vs.glsl";
	EVSM.fs = "shaders/shadowmap/fs.glsl";
	EVSM.fsPreProcessor = "#define USE_EVSM 1\r\n#define USE_TEXTURE";
	EVSM_Shader = jShader::CreateShader(EVSM);

	SSM.name = "SSM";
	SSM.vs = "shaders/shadowmap/vs.glsl";
	SSM.fs = "shaders/shadowmap/fs.glsl";
	SSM.vsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	SSM.fsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	SSM_Shader = jShader::CreateShader(SSM);

	Hair.name = "Hair";
	Hair.vs = "shaders/shadowmap/vs_hair.glsl";
	Hair.fs = "shaders/shadowmap/fs_hair.glsl";
	Hair_Shader = jShader::CreateShader(Hair);

	// Shadow Gen
	ShadowGen_VSM.name = "ShadowGen_VSM";
	ShadowGen_VSM.vs = "shaders/shadowmap/vs_varianceShadowMap.glsl";
	ShadowGen_VSM.fs = "shaders/shadowmap/fs_varianceShadowMap.glsl";
	ShadowGen_VSM_Shader = jShader::CreateShader(ShadowGen_VSM);

	ShadowGen_ESM.name = "ShadowGen_ESM";
	ShadowGen_ESM.vs = "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl";
	ShadowGen_ESM.fs = "shaders/shadowmap/fs_exponentialShadowMap.glsl";
	ShadowGen_ESM_Shader = jShader::CreateShader(ShadowGen_ESM);

	ShadowGen_EVSM.name = "ShadowGen_EVSM";
	ShadowGen_EVSM.vs = "shaders/shadowmap/vs_EVSM.glsl";
	ShadowGen_EVSM.fs = "shaders/shadowmap/fs_EVSM.glsl";
	ShadowGen_EVSM_Shader = jShader::CreateShader(ShadowGen_EVSM);

	ShadowGen_SSM.name = "ShadowGen_SSM";
	ShadowGen_SSM.vs = "shaders/shadowmap/vs_shadowMap.glsl";
	ShadowGen_SSM.fs = "shaders/shadowmap/fs_shadowMap.glsl";
	ShadowGen_SSM_Shader = jShader::CreateShader(ShadowGen_SSM);

	ShadowGen_Omni_ESM.name = "ShadowGen_Omni_ESM";
	ShadowGen_Omni_ESM.vs = "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl";
	ShadowGen_Omni_ESM.fs = "shaders/shadowmap/fs_omniDirectionalExponentialShadowMap.glsl";
	ShadowGen_Omni_ESM_Shader = jShader::CreateShader(ShadowGen_Omni_ESM);

	ShadowGen_Omni_EVSM.name = "ShadowGen_Omni_EVSM";
	ShadowGen_Omni_EVSM.vs = "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl";
	ShadowGen_Omni_EVSM.fs = "shaders/shadowmap/fs_omniDirectionalEVSM.glsl";
	ShadowGen_Omni_EVSM_Shader = jShader::CreateShader(ShadowGen_Omni_EVSM);

	ShadowGen_Omni_SSM.name = "ShadowGen_Omni_SSM";
	ShadowGen_Omni_SSM.vs = "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl";
	ShadowGen_Omni_SSM.fs = "shaders/shadowmap/fs_omniDirectionalShadowMap.glsl";
	ShadowGen_Omni_SSM_Shader = jShader::CreateShader(ShadowGen_Omni_SSM);

	UIShadowInfo.name = "UI";
	UIShadowInfo.vs = "Shaders/tex_ui_vs.glsl";
	UIShadowInfo.fs = "Shaders/tex_ui_fs.glsl";
	UIShader = jShader::CreateShader(UIShadowInfo);
}

void jShadowMapRenderer::UpdateSettings()
{
	if (ShadowMapType != jShadowAppSettingProperties::GetInstance().ShadowMapType)
	{
		ShadowMapType = jShadowAppSettingProperties::GetInstance().ShadowMapType;
	}
	jShadowAppSettingProperties::GetInstance().SwitchShadowMapType(jAppSettings::GetInstance().Get("MainPannel"));
	UsePoissonSample = jShadowAppSettingProperties::GetInstance().UsePoissonSample;
}

void jShadowMapRenderer::DebugRenderPass(const jCamera* camera)
{
	static jShader* texShader = nullptr;
	if (!texShader)
	{
		jShaderInfo info;
		info.vs = "Shaders/tex_vs.glsl";
		info.fs = "Shaders/tex_fs.glsl";
		texShader = jShader::CreateShader(info);
	}

	g_rhi->EnableBlend(true);
	g_rhi->SetBlendFunc(EBlendSrc::SRC_ALPHA, EBlendDest::ONE_MINUS_SRC_ALPHA);

	const auto ShowDirectionalLightInfo = jShadowAppSettingProperties::GetInstance().ShowDirectionalLightInfo;
	const auto ShowPointLightInfo = jShadowAppSettingProperties::GetInstance().ShowPointLightInfo;
	const auto ShowSpotLightInfo = jShadowAppSettingProperties::GetInstance().ShowSpotLightInfo;

	for (auto& iter : camera->LightList)
	{
		if (iter->LightDebugObject)
		{
			if (iter->Type == ELightType::DIRECTIONAL && !ShowDirectionalLightInfo)
				continue;
			if (iter->Type == ELightType::POINT && !ShowPointLightInfo)
				continue;
			if (iter->Type == ELightType::SPOT && !ShowSpotLightInfo)
				continue;

			iter->LightDebugObject->Draw(camera, texShader, {});
		}
	}
	g_rhi->EnableBlend(false);

	__super::DebugRenderPass(camera);
}
