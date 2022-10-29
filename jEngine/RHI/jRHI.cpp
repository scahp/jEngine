#include "pch.h"
#include "jRHI.h"
#include "Shader/jShader.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/jCamera.h"

//////////////////////////////////////////////////////////////////////////
void IUniformBuffer::Bind(const jShader* shader) const
{
	SetUniformbuffer(shader);
}

//////////////////////////////////////////////////////////////////////////

jRHI::jRHI()
{
}

void jQueryPrimitiveGenerated::Begin() const
{
	g_rhi->BeginQueryPrimitiveGenerated(this);
}

void jQueryPrimitiveGenerated::End() const
{
	g_rhi->EndQueryPrimitiveGenerated();
}

uint64 jQueryPrimitiveGenerated::GetResult()
{
	g_rhi->GetQueryPrimitiveGeneratedResult(this);
	return NumOfGeneratedPrimitives;
}

jName GetCommonTextureName(int32 index)
{
	static std::vector<jName> s_tex_name;

	if ((s_tex_name.size() > index) && s_tex_name[index].IsValid())
	{
		return s_tex_name[index];
	}

	if (s_tex_name.size() < (index + 1))
		s_tex_name.resize(index + 1);

	char szTemp[128] = { 0, };
	if (index > 0)
		sprintf_s(szTemp, sizeof(szTemp), "tex_object%d", index + 1);
	else
		sprintf_s(szTemp, sizeof(szTemp), "tex_object");

	s_tex_name[index] = jName(szTemp);
	return s_tex_name[index];
}

jName GetCommonTextureSRGBName(int32 index)
{
	static std::vector<jName> s_tex_srgb_name;

	if ((s_tex_srgb_name.size() > index) && s_tex_srgb_name[index].IsValid())
	{
		return s_tex_srgb_name[index];
	}

	if (s_tex_srgb_name.size() < (index + 1))
		s_tex_srgb_name.resize(index + 1);

	char szTemp[128] = { 0, };
	sprintf_s(szTemp, sizeof(szTemp), "TextureSRGB[%d]", index);

	s_tex_srgb_name[index] = jName(szTemp);
	return s_tex_srgb_name[index];
}

size_t jRenderPass::GetHash() const
{
	if (Hash)
		return Hash;

	Hash = RenderPassInfo.GetHash();
	Hash = CityHash64WithSeed((const char*)&RenderOffset, sizeof(RenderOffset), Hash);
	Hash = CityHash64WithSeed((const char*)&RenderExtent, sizeof(RenderExtent), Hash);
	return Hash;
}

size_t jShaderBindingsLayout::GetHash() const
{
	if (Hash)
		return Hash;
	
	Hash = ShaderBindingArray.GetHash();
	return Hash;
}

void jView::GetShaderBindingInstance(jShaderBindingInstanceArray& OutShaderBindingInstanceArray)
{
	if (DirectionalLight.Light)
	{
		check(DirectionalLight.ShaderBindingInstance);
		OutShaderBindingInstanceArray.Add(DirectionalLight.ShaderBindingInstance);
	}
}
