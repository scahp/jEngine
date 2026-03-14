#include "pch.h"
#include "jRHIUtil.h"
#include "FileLoader/jImageFileLoader.h"
#include "jRenderTargetPool.h"
#include "DX12/jTexture_DX12.h"
#include "Renderer/jSceneRenderTargets.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "Scene/jRenderObject.h"
#include "Shader/jShaderParameterSet.h"

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jCubeMapMipUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, height)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, mip)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, maxMip)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jGenCubemapFromSphericalProbeCSParameters)
    SHADER_TEXTURE2D(EnvMap)
    SHADER_RW_TEXTURE2DARRAY(Result)
    SHADER_UNIFORM_BUFFER(jCubeMapMipUniformBuffer, MipParam)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jIrradianceMapSizeUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, height)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jGenIrradianceMapCSParameters)
    SHADER_TEXTURECUBE(TexHDR)
    SHADER_RW_TEXTURE2DARRAY(IrradianceMap)
    SHADER_UNIFORM_BUFFER(jIrradianceMapSizeUniformBuffer, RTSizeParam)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jGenFilteredEnvMapCSParameters)
    SHADER_TEXTURECUBE(TexHDR)
    SHADER_RW_TEXTURE2DARRAY(Result)
    SHADER_UNIFORM_BUFFER(jCubeMapMipUniformBuffer, MipParam)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jCopyCSUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding1)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jCopyCSParameters)
    SHADER_RW_TEXTURE2D(resultImage)
    SHADER_TEXTURE2D(inputImage)
    SHADER_UNIFORM_BUFFER(jCopyCSUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSingleTexturePSParameters)
    SHADER_TEXTURE2D(Texture)
END_SHADER_PARAMETER_SET()

namespace
{
template <typename TShaderParameters>
void DispatchShaderParameterComputePass(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jName InShaderPath
    , const TShaderParameters& InParameters, uint32 NumGroupsX, uint32 NumGroupsY, uint32 NumGroupsZ)
{
    auto CurrentBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
        InParameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

    jShaderInfo ShaderInfo;
    ShaderInfo.SetName(InShaderPath);
    ShaderInfo.SetShaderFilepath(InShaderPath);
    ShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    jShaderParameterSet::AppendToShaderInfo<TShaderParameters>(ShaderInfo, 0);
    jShader* Shader = g_rhi->CreateShader(ShaderInfo);

    jShaderBindingLayoutArray ShaderBindingLayoutArray;
    ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

    jPipelineStateInfo* ComputePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});
    ComputePipelineStateInfo->Bind(InRenderFrameContextPtr);

    jShaderBindingInstanceArray ShaderBindingInstanceArray;
    ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

    jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
        const std::vector<uint32>* DynamicOffsets = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
        if (DynamicOffsets && DynamicOffsets->size())
        {
            ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
        }
    }
    ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

    g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingInstanceCombiner, 0);
    g_rhi->DispatchCompute(InRenderFrameContextPtr, NumGroupsX, NumGroupsY, NumGroupsZ);
}
}

namespace jRHIUtil
{


std::shared_ptr<jRenderTarget> ConvertToCubeMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jName InTwoMirrorBallSphereMapFilePath)
{
    check(IsUseDX12());
    check(InDestFilePath.IsValid());
    check(InTwoMirrorBallSphereMapFilePath.IsValid());
    check(InRenderFrameContextPtr);

    jTexture* Spheremap = jImageFileLoader::GetInstance().LoadTextureFromFile(InTwoMirrorBallSphereMapFilePath).lock().get();
    check(Spheremap);

    return ConvertToCubeMap(InDestFilePath, InDestTextureSize, InRenderFrameContextPtr, Spheremap);
}

std::shared_ptr<jRenderTarget> ConvertToCubeMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jTexture* InTwoMirrorBallSphereMap)
{
    check(IsUseDX12());
    check(InDestFilePath.IsValid());
    check(InTwoMirrorBallSphereMap);

    // if it is zero size, set the spheremap texture size
    if (InDestTextureSize.x == 0)
        InDestTextureSize.x = InTwoMirrorBallSphereMap->Width;
    if (InDestTextureSize.y == 0)
        InDestTextureSize.y = InTwoMirrorBallSphereMap->Height;

    static jRenderTargetInfo Info = {
        .Type = ETextureType::TEXTURE_CUBE,
        .Format = ETextureFormat::RGBA16F,
        .Width = InDestTextureSize.x,
        .Height = InDestTextureSize.y,
        .LayerCount = 6,
        .IsGenerateMipmap = true,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .IsUseAsSubpassInput = false,
        .IsMemoryless = false,
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        .TextureCreateFlag = ETextureCreateFlag::RTV | ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("Cubemap")
    };
    auto CubeMap = jRenderTargetPool::GetRenderTarget(Info);
    {
        DEBUG_EVENT(InRenderFrameContextPtr, "DrawCubemap");

        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTwoMirrorBallSphereMap, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), CubeMap->GetTexture(), EResourceLayout::UAV);

        jCubeMapMipUniformBuffer MipUBO;
        MipUBO.maxMip = jTexture::GetMipLevels(CubeMap->Info.Width, CubeMap->Info.Height);

        for (int32 i = 0; i < MipUBO.maxMip; ++i)
        {
            MipUBO.width = CubeMap->Info.Width >> i;
            MipUBO.height = CubeMap->Info.Height >> i;
            MipUBO.mip = i;

            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
                g_rhi->CreateUniformBufferBlock(jNameStatic("MipUniformBuffer"), jLifeTimeType::OneFrame, sizeof(MipUBO)));
            OneFrameUniformBuffer->UpdateBufferData(&MipUBO, sizeof(MipUBO));

            jGenCubemapFromSphericalProbeCSParameters Parameters;
            Parameters.EnvMap = { InTwoMirrorBallSphereMap, nullptr };
            Parameters.Result = { CubeMap->GetTexture(), i };
            Parameters.MipParam.Buffer = OneFrameUniformBuffer;

            DispatchShaderParameterComputePass(InRenderFrameContextPtr, jNameStatic("Resource/Shaders/hlsl/gencubemapfromsphericalprobe_cs.hlsl"), Parameters
                , MipUBO.width / 16 + ((MipUBO.width % 16) ? 1 : 0)
                , MipUBO.height / 16 + ((MipUBO.height % 16) ? 1 : 0)
                , 6);
        }
    }

    // Flush all render command
    InRenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None);
    InRenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
    g_rhi->Flush();

    // Capture image form texture
    DirectX::ScratchImage image;
    jTexture_DX12* texture_dx12 = (jTexture_DX12*)CubeMap->GetTexture();
    DirectX::CaptureTexture(g_rhi_dx12->GraphicsCommandBufferManager->GetCommandQueue().Get()
        , texture_dx12->Texture->Get(), true, image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const std::wstring DestFilePath = ConvertToWchar(InDestFilePath);

    // Save image
    DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DestFilePath.c_str());
    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), CubeMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

    return CubeMap;
}

std::shared_ptr<jRenderTarget> GenerateIrradianceMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jTexture* InCubemap)
{
    check(InDestFilePath.IsValid());
    check(InCubemap);
    check(InRenderFrameContextPtr);

    // if it is zero size, set the spheremap texture size
    if (InDestTextureSize.x == 0)
        InDestTextureSize.x = InCubemap->Width;
    if (InDestTextureSize.y == 0)
        InDestTextureSize.y = InCubemap->Height;

    static jRenderTargetInfo Info = {
        .Type = ETextureType::TEXTURE_CUBE,
        .Format = ETextureFormat::RGBA16F,
        .Width = InDestTextureSize.x,
        .Height = InDestTextureSize.y,
        .LayerCount = 6,
        .IsGenerateMipmap = false,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .IsUseAsSubpassInput = false,
        .IsMemoryless = false,
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        .TextureCreateFlag = ETextureCreateFlag::RTV | ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("IrradianceMap")
    };
    auto IrradianceMap = jRenderTargetPool::GetRenderTarget(Info);
    {
        DEBUG_EVENT(InRenderFrameContextPtr, "GenIrradianceMap");

        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InCubemap, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), IrradianceMap->GetTexture(), EResourceLayout::UAV);

        jIrradianceMapSizeUniformBuffer RTSizeUBO;
        RTSizeUBO.width = Info.Width;
        RTSizeUBO.height = Info.Height;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("MipUniformBuffer"), jLifeTimeType::OneFrame, sizeof(RTSizeUBO)));
        OneFrameUniformBuffer->UpdateBufferData(&RTSizeUBO, sizeof(RTSizeUBO));

        jGenIrradianceMapCSParameters Parameters;
        Parameters.TexHDR = { InCubemap, nullptr };
        Parameters.IrradianceMap = { IrradianceMap->GetTexture() };
        Parameters.RTSizeParam.Buffer = OneFrameUniformBuffer;

        int32 X = (Info.Width / 16) + ((Info.Width % 16) ? 1 : 0);
        int32 Y = (Info.Height / 16) + ((Info.Height % 16) ? 1 : 0);
        DispatchShaderParameterComputePass(InRenderFrameContextPtr, jNameStatic("Resource/Shaders/hlsl/genirradiancemap_cs.hlsl"), Parameters, X, Y, 6);
    }

    // Flush all render command
    InRenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::BasePass);
    InRenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
    g_rhi->Flush();

    // Capture image form texture
    DirectX::ScratchImage image;
    jTexture_DX12* texture_dx12 = (jTexture_DX12*)IrradianceMap->GetTexture();
    DirectX::CaptureTexture(g_rhi_dx12->GraphicsCommandBufferManager->GetCommandQueue().Get()
        , texture_dx12->Texture->Get(), true, image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const std::wstring DestFilePath = ConvertToWchar(InDestFilePath);

    // Save image
    DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DestFilePath.c_str());
    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), IrradianceMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

    return IrradianceMap;
}

std::shared_ptr<jRenderTarget> GenerateFilteredEnvironmentMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jTexture* InCubemap)
{
    check(InDestFilePath.IsValid());
    check(InCubemap);
    check(InRenderFrameContextPtr);

    // if it is zero size, set the spheremap texture size
    if (InDestTextureSize.x == 0)
        InDestTextureSize.x = InCubemap->Width;
    if (InDestTextureSize.y == 0)
        InDestTextureSize.y = InCubemap->Height;

    static jRenderTargetInfo Info = {
        .Type = ETextureType::TEXTURE_CUBE,
        .Format = ETextureFormat::RGBA16F,
        .Width = InDestTextureSize.x,
        .Height = InDestTextureSize.y,
        .LayerCount = 6,
        .IsGenerateMipmap = true,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .IsUseAsSubpassInput = false,
        .IsMemoryless = false,
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        .TextureCreateFlag = ETextureCreateFlag::RTV | ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("FilteredEnvMap")
    };
    auto FilteredEnvMap = jRenderTargetPool::GetRenderTarget(Info);
    {
        DEBUG_EVENT(InRenderFrameContextPtr, "GenFilteredEnvMap");

        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InCubemap, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), FilteredEnvMap->GetTexture(), EResourceLayout::UAV);

        jCubeMapMipUniformBuffer MipUBO;
        MipUBO.maxMip = jTexture::GetMipLevels(FilteredEnvMap->Info.Width, FilteredEnvMap->Info.Height);

        for (int32 i = 0; i < MipUBO.maxMip; ++i)
        {
            MipUBO.width = FilteredEnvMap->Info.Width >> i;
            MipUBO.height = FilteredEnvMap->Info.Height >> i;
            MipUBO.mip = i;

            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("MipUniformBuffer"), jLifeTimeType::OneFrame, sizeof(MipUBO)));
            OneFrameUniformBuffer->UpdateBufferData(&MipUBO, sizeof(MipUBO));

            jGenFilteredEnvMapCSParameters Parameters;
            Parameters.TexHDR = { InCubemap, nullptr };
            Parameters.Result = { FilteredEnvMap->GetTexture(), i };
            Parameters.MipParam.Buffer = OneFrameUniformBuffer;

            int32 X = (MipUBO.width / 16) + ((MipUBO.width % 16) ? 1 : 0);
            int32 Y = (MipUBO.height / 16) + ((MipUBO.height % 16) ? 1 : 0);
            DispatchShaderParameterComputePass(InRenderFrameContextPtr, jNameStatic("Resource/Shaders/hlsl/genprefilteredenvmap_cs.hlsl"), Parameters, X, Y, 6);
        }
    }

    // Flush all render command
    InRenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::BasePass);
    InRenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
    g_rhi->Flush();

    // Capture from texture
    DirectX::ScratchImage image;
    jTexture_DX12* texture_dx12 = (jTexture_DX12*)FilteredEnvMap->GetTexture();
    DirectX::CaptureTexture(g_rhi_dx12->GraphicsCommandBufferManager->GetCommandQueue().Get()
        , texture_dx12->Texture->Get(), true, image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const std::wstring DestFilePath = ConvertToWchar(InDestFilePath);

    // Save image
    DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DestFilePath.c_str());
    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), FilteredEnvMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

    return FilteredEnvMap;
}

void CopyTexture2D(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jTexture* InDestTexture, jTexture* InSourceTexture)
{
    check(InRenderFrameContextPtr);
    check(InDestTexture);
    check(InSourceTexture);

    jCopyCSUniformBuffer UniformBufferData;
    UniformBufferData.Width = InDestTexture->Width;
    UniformBufferData.Height = InDestTexture->Height;

    auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("CopyCSOneFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformBufferData)));
    OneFrameUniformBuffer->UpdateBufferData(&UniformBufferData, sizeof(UniformBufferData));

    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InSourceTexture, EResourceLayout::SHADER_READ_ONLY);

    jCopyCSParameters Parameters;
    Parameters.resultImage = { InDestTexture };
    Parameters.inputImage = { InSourceTexture, nullptr };
    Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

    const int32 X = (InDestTexture->Width / 8) + ((InDestTexture->Width % 8) ? 1 : 0);
    const int32 Y = (InDestTexture->Height / 8) + ((InDestTexture->Height % 8) ? 1 : 0);
    DispatchShaderParameterComputePass(InRenderFrameContextPtr, jNameStatic("Resource/Shaders/hlsl/copy_cs.hlsl"), Parameters, X, Y, 1);
}

void BuildSingleTextureFragmentBindings(jTexture* InTexture, const jSamplerStateInfo* InSamplerState
    , jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllocator)
{
    jSingleTexturePSParameters Parameters;
    Parameters.Texture.Texture = InTexture;
    Parameters.Texture.SamplerState = InSamplerState;
    jShaderParameterSet::BuildShaderBindings(Parameters, EShaderAccessStageFlag::FRAGMENT, InOutShaderBindingArray, InOutResourceInlineAllocator);
}

void AppendSingleTextureFragmentShaderInfo(jShaderInfo& InOutShaderInfo, int32 InSpace)
{
    jShaderParameterSet::AppendToShaderInfo<jSingleTexturePSParameters>(InOutShaderInfo, InSpace);
}

void CreateDefaultFixedPipelineStates(jRasterizationStateInfo*& OutRasterState, jBlendingStateInfo*& OutBlendState, jDepthStencilStateInfo*& OutDepthStencilState)
{
    OutRasterState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
    OutDepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    OutBlendState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();
}

void DispatchCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jTexture* RenderTarget, FuncBindingShaderResources InFuncBindingShaderResources, FuncCreateShaders InFuncCreateShaders)
{
	check(RenderTarget);
	const int32 Width = RenderTarget->Width;
	const int32 Height = RenderTarget->Height;

	std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
	jShaderBindingArray ShaderBindingArray;
	jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

	g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), RenderTarget, EResourceLayout::UAV);

	ShaderBindingArray.Add(jShaderBinding::Create(ShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
		, ResourceInlineAllactor.Alloc<jTextureResource>(RenderTarget, nullptr)));

	InFuncBindingShaderResources(InRenderFrameContextPtr, ShaderBindingArray, ResourceInlineAllactor);

	CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

	jShaderBindingLayoutArray ShaderBindingLayoutArray;
	ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

	jShader* Shader = InFuncCreateShaders(InRenderFrameContextPtr);
	jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

	computePipelineStateInfo->Bind(InRenderFrameContextPtr);

	jShaderBindingInstanceArray ShaderBindingInstanceArray;
	ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

	jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
	for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
	{
		// Add ShaderBindingInstanceCombiner data : DescriptorSets, DynamicOffsets
		ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
		const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
		if (pDynamicOffsetTest && pDynamicOffsetTest->size())
		{
			ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
		}
	}
	ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

	g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), computePipelineStateInfo, ShaderBindingInstanceCombiner, 0);

	const int32 X = (Width / 8) + ((Width % 8) ? 1 : 0);
	const int32 Y = (Height / 8) + ((Height % 8) ? 1 : 0);
	g_rhi->DispatchCompute(InRenderFrameContextPtr, X, Y, 1);
}

void DrawFullScreen(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, std::shared_ptr<jRenderTarget> InRenderTargetPtr
                    , FuncBindingShaderResources InFuncBindingShaderResources, FuncCreateShaders InFuncCreateShaders, FuncCreateFixedPipelineStates InFuncCreateFixedPipelineStates)
{
    check(InRenderTargetPtr);
	const int32 RTWidth = InRenderTargetPtr->Info.Width;
	const int32 RTHeight = InRenderTargetPtr->Info.Height;
	DrawQuad(InRenderFrameContextPtr, InRenderTargetPtr, Vector4i(0, 0, RTWidth, RTHeight), InFuncBindingShaderResources, InFuncCreateShaders, InFuncCreateFixedPipelineStates);
}

void DrawQuad(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, std::shared_ptr<jRenderTarget> InRenderTargetPtr, Vector4i InRect
    , FuncBindingShaderResources InFuncBindingShaderResources, FuncCreateShaders InFuncCreateShaders, FuncCreateFixedPipelineStates InFuncCreateFixedPipelineStates)
{
	check(InRenderTargetPtr);
	const int32 Width = InRenderTargetPtr->Info.Width;
	const int32 Height = InRenderTargetPtr->Info.Height;

	g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTargetPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);

	jRasterizationStateInfo* RasterizationState = nullptr;
	jBlendingStateInfo* BlendingState = nullptr;
	jDepthStencilStateInfo* DepthStencilState = nullptr;
	InFuncCreateFixedPipelineStates(RasterizationState, BlendingState, DepthStencilState);

	// Create fixed pipeline states
	jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
		, jViewport(InRect.x, InRect.y, InRect.z, InRect.w), jScissor(InRect.x, InRect.y, InRect.z, InRect.w), gOptions.UseVRS);

	const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
	const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

	jRenderPassInfo renderPassInfo;
	jAttachment color = {
		.RenderTargetPtr = InRenderTargetPtr,
		.LoadStoreOp = EAttachmentLoadStoreOp::LOAD_STORE,
		.StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
		.RTClearValue = ClearColor,
		.InitialLayout = InRenderTargetPtr->GetLayout(),
		.FinalLayout = EResourceLayout::COLOR_ATTACHMENT
	};
	renderPassInfo.Attachments.push_back(color);

	jSubpass subpass;
	subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
	subpass.OutputColorAttachments.push_back(0);
	renderPassInfo.Subpasses.push_back(subpass);

	auto RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { Width, Height });

	std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
	int32 BindingPoint = 0;
	jShaderBindingArray ShaderBindingArray;
	jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

	InFuncBindingShaderResources(InRenderFrameContextPtr, ShaderBindingArray, ResourceInlineAllactor);

	std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
	jShaderBindingInstanceArray ShaderBindingInstanceArray;
	ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());

	jGraphicsPipelineShader Shader;
	{
		Shader.VertexShader = jShaderFullscreenQuadVertexShader::CreateShader(jShaderFullscreenQuadVertexShader::ShaderPermutation());

		Shader.PixelShader = InFuncCreateShaders(InRenderFrameContextPtr);
	}

	if (!jSceneRenderTarget::GlobalFullscreenPrimitive)
		jSceneRenderTarget::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
	jDrawCommand DrawCommand(InRenderFrameContextPtr, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
		, Shader, &PostProcessPassPipelineStateFixed, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
	DrawCommand.Test = true;
	DrawCommand.PrepareToDraw(false);
	if (RenderPass && RenderPass->BeginRenderPass(InRenderFrameContextPtr->GetActiveCommandBuffer()))
	{
		DrawCommand.Draw();
		RenderPass->EndRenderPass();
	}
}

}
