#include "pch.h"
#include "jPathTracingLight.h"

void jPathTracingLight::Initialize(const jPathTracingLightUniformBufferData& InData)
{
	LightData = InData;
}

const std::shared_ptr<jShaderBindingInstance>& jPathTracingLight::PrepareShaderBindingInstance(jTexture* InShadowMap)
{
	if (IsNeedToUpdateShaderBindingInstance)
	{
		IsNeedToUpdateShaderBindingInstance = false;

		LightDataUniformBufferPtr = std::shared_ptr<IUniformBufferBlock>(
			g_rhi->CreateUniformBufferBlock(jNameStatic("PathTracingLightBlock"), jLifeTimeType::MultiFrame, sizeof(jPathTracingLightUniformBufferData)));
		LightDataUniformBufferPtr->UpdateBufferData(&LightData, sizeof(LightData));

		int32 BindingPoint = 0;
		jShaderBindingArray ShaderBindingArray;
		jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

		ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
			, ResourceInlineAllocator.Alloc<jUniformBufferResource>(LightDataUniformBufferPtr.get())));

		if (ShaderBindingInstanceDataPtr)
			ShaderBindingInstanceDataPtr->Free();
		ShaderBindingInstanceDataPtr = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
	}

	return ShaderBindingInstanceDataPtr;
}
