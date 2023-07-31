#include "pch.h"
#include "jRHI.h"
#include "Shader/jShader.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/jCamera.h"
#include "Vulkan/jUniformBufferBlock_Vulkan.h"
#include "Material/jMaterial.h"

jTexture* GWhiteTexture = nullptr;
jTexture* GBlackTexture = nullptr;
jTexture* GNormalTexture = nullptr;
jMaterial* GDefaultMaterial = nullptr;

//////////////////////////////////////////////////////////////////////////
void IUniformBuffer::Bind(const jShader* shader) const
{
	SetUniformbuffer(shader);
}

//////////////////////////////////////////////////////////////////////////
TResourcePool<jShader, jMutexRWLock> jRHI::ShaderPool;
jVertexBuffer* jRHI::CubeMapInstanceDataForSixFace = nullptr;

bool jRHI::InitRHI()
{
    return false;
}

void jRHI::OnInitRHI()
{
	uint8 WhiteData[4] = { 255, 255, 255, 255 };
	GWhiteTexture = CreateTextureFromData(&WhiteData, 1, 1, false, ETextureFormat::RGBA8, false);

	uint8 BlackData[4] = { 0, 0, 0, 0 };
	GBlackTexture = CreateTextureFromData(&BlackData, 1, 1, false, ETextureFormat::RGBA8, false);

    uint8 NormalData[4] = { 0, 0, 255, 0 };
	GNormalTexture = CreateTextureFromData(&NormalData, 1, 1, false, ETextureFormat::RGBA8, false);

	GDefaultMaterial = new jMaterial();
	for (int32 i = 0; i < _countof(GDefaultMaterial->TexData); ++i)
		GDefaultMaterial->TexData[i].Texture = GWhiteTexture;
}

void jRHI::ReleaseRHI()
{
	delete GWhiteTexture;
	delete GBlackTexture;
	delete GNormalTexture;
	ShaderPool.Release();
}

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

jView::jView(const jCamera* camera, const std::vector<jLight*>& InLights)
	: Camera(camera)
{
    check(camera);
	Lights.resize(InLights.size());
	for (int32 i=0;i<(int32)Lights.size();++i)
	{
		new(&Lights[i]) jViewLight(InLights[i]);
	}
}

void jView::PrepareViewUniformBufferShaderBindingInstance()
{
    // Prepare & Get ViewUniformBuffer
    struct jViewUniformBuffer
    {
        Matrix V;
        Matrix P;
        Matrix VP;
		Vector EyeWorld;
		float padding0;
    };

    jViewUniformBuffer ubo;
    ubo.P = Camera->Projection;
    ubo.V = Camera->View;
    ubo.VP = Camera->Projection * Camera->View;
	ubo.EyeWorld = Camera->Pos;

    ViewUniformBufferPtr = std::make_shared<jUniformBufferBlock_Vulkan>(jNameStatic("ViewUniformParameters"), jLifeTimeType::OneFrame);
    ViewUniformBufferPtr->Init(sizeof(ubo));
    ViewUniformBufferPtr->UpdateBufferData(&ubo, sizeof(ubo));

    int32 BindingPoint = 0;
    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

    ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
        , ResourceInlineAllactor.Alloc<jUniformBufferResource>(ViewUniformBufferPtr.get()));

	ViewUniformBufferShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
}

void jView::GetShaderBindingInstance(jShaderBindingInstanceArray& OutShaderBindingInstanceArray, bool InIsForwardRenderer) const
{
	OutShaderBindingInstanceArray.Add(ViewUniformBufferShaderBindingInstance);

	// Get light uniform buffers
	if (InIsForwardRenderer)
	{
		for (int32 i = 0; i < Lights.size(); ++i)
		{
			const jViewLight& ViewLight = Lights[i];
			if (ViewLight.Light)
			{
				check(ViewLight.ShaderBindingInstance);
				OutShaderBindingInstanceArray.Add(ViewLight.ShaderBindingInstance);
			}
		}
	}
}
