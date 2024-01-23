#include "pch.h"
#include "jRHI.h"
#include "Shader/jShader.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/jCamera.h"
#include "Material/jMaterial.h"
#include "jRHIType.h"
#include "FileLoader/jImageFileLoader.h"

const jRTClearValue jRTClearValue::Invalid = jRTClearValue();

std::shared_ptr<jTexture> GWhiteTexture;
std::shared_ptr<jTexture> GBlackTexture;
std::shared_ptr<jTexture> GWhiteCubeTexture;
std::shared_ptr<jTexture> GNormalTexture;
std::shared_ptr<jTexture> GRoughnessMetalicTexture;
std::shared_ptr<jMaterial> GDefaultMaterial = nullptr;

//////////////////////////////////////////////////////////////////////////
void IUniformBuffer::Bind(const jShader* shader) const
{
	SetUniformbuffer(shader);
}

//////////////////////////////////////////////////////////////////////////
TResourcePool<jShader, jMutexRWLock> jRHI::ShaderPool;
std::shared_ptr<jVertexBuffer> jRHI::CubeMapInstanceDataForSixFace;

// jGPUDebugEvent
jGPUDebugEvent::jGPUDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName)
{
    CommandBuffer = InCommandBuffer;
    g_rhi->BeginDebugEvent(CommandBuffer, InName);
}

jGPUDebugEvent::jGPUDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName, const Vector4& InColor)
{
    CommandBuffer = InCommandBuffer;
    g_rhi->BeginDebugEvent(CommandBuffer, InName, InColor);
}

jGPUDebugEvent::~jGPUDebugEvent()
{
    g_rhi->EndDebugEvent(CommandBuffer);
}

bool jRHI::InitRHI()
{
    return false;
}

void jRHI::OnInitRHI()
{
    jImageData image;
    image.ImageBulkData.ImageData = { 255, 255, 255, 255 };
    image.Width = 1;
    image.Height = 1;
    image.Format = ETextureFormat::RGBA8;
    image.FormatType = EFormatType::UNSIGNED_BYTE;
    image.sRGB = false;

    image.ImageBulkData.ImageData = { 255, 255, 255, 255 };
    GWhiteTexture = CreateTextureFromData(&image);

    image.ImageBulkData.ImageData = { 0, 0, 0, 255 };
    GBlackTexture = CreateTextureFromData(&image);

    image.ImageBulkData.ImageData = { 255 / 2, 255 / 2, 255, 0 };
    GNormalTexture = CreateTextureFromData(&image);

    const float roughness = 0.2f;
    const float metalic = 0.0f;
    image.ImageBulkData.ImageData = { 0, (uint8)(255.0f * roughness), (uint8)(255.0f * metalic), 0 };
    GRoughnessMetalicTexture = CreateTextureFromData(&image);

    image.TextureType = ETextureType::TEXTURE_CUBE;
    image.LayerCount = 6;
    image.ImageBulkData.ImageData = { 255, 255, 255, 200, 255, 255 };
    GWhiteCubeTexture = CreateTextureFromData(&image);

    GDefaultMaterial = std::make_shared<jMaterial>();
    GDefaultMaterial->TexData[(int32)jMaterial::EMaterialTextureType::Albedo].Texture = GWhiteTexture.get();
    GDefaultMaterial->TexData[(int32)jMaterial::EMaterialTextureType::Normal].Texture = GNormalTexture.get();
    GDefaultMaterial->TexData[(int32)jMaterial::EMaterialTextureType::Metallic].Texture = GRoughnessMetalicTexture.get();    
}

void jRHI::ReleaseRHI()
{
    jImageFileLoader::ReleaseInstance();

    GWhiteTexture.reset();
    GBlackTexture.reset();
    GWhiteCubeTexture.reset();
    GNormalTexture.reset();
    GRoughnessMetalicTexture.reset();
    GDefaultMaterial.reset();

    jShader::ReleaseCheckUpdateShaderThread();
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

size_t jShaderBindingLayout::GetHash() const
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

    ViewUniformBufferPtr = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("ViewUniformParameters"), jLifeTimeType::OneFrame, sizeof(ubo)));
    ViewUniformBufferPtr->UpdateBufferData(&ubo, sizeof(ubo));

    int32 BindingPoint = 0;
    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
        , ResourceInlineAllactor.Alloc<jUniformBufferResource>(ViewUniformBufferPtr.get()), true));

	ViewUniformBufferShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
}

void jView::GetShaderBindingInstance(jShaderBindingInstanceArray& OutShaderBindingInstanceArray, bool InIsForwardRenderer) const
{
	OutShaderBindingInstanceArray.Add(ViewUniformBufferShaderBindingInstance.get());

	// Get light uniform buffers
	if (InIsForwardRenderer)
	{
		for (int32 i = 0; i < Lights.size(); ++i)
		{
			const jViewLight& ViewLight = Lights[i];
			if (ViewLight.Light)
			{
				check(ViewLight.ShaderBindingInstance);
				OutShaderBindingInstanceArray.Add(ViewLight.ShaderBindingInstance.get());
			}
		}
	}
}

void jView::GetShaderBindingLayout(jShaderBindingLayoutArray& OutShaderBindingsLayoutArray, bool InIsForwardRenderer /*= false*/) const
{
	OutShaderBindingsLayoutArray.Add(ViewUniformBufferShaderBindingInstance->ShaderBindingsLayouts);

    // Get light uniform buffers
    if (InIsForwardRenderer)
    {
        for (int32 i = 0; i < Lights.size(); ++i)
        {
            const jViewLight& ViewLight = Lights[i];
            if (ViewLight.Light)
            {
                check(ViewLight.ShaderBindingInstance);
				OutShaderBindingsLayoutArray.Add(ViewLight.ShaderBindingInstance->ShaderBindingsLayouts);
            }
        }
    }
}
