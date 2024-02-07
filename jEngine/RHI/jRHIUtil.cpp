#include "pch.h"
#include "jRHIUtil.h"
#include "FileLoader/jImageFileLoader.h"
#include "jRenderTargetPool.h"
#include "DX12/jTexture_DX12.h"
#include "Renderer/jSceneRenderTargets.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "Scene/jRenderObject.h"

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

    static jRenderTargetInfo Info = { ETextureType::TEXTURE_CUBE, ETextureFormat::RGBA16F, InDestTextureSize.x, InDestTextureSize.y
        , 6, true, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f), ETextureCreateFlag::RTV | ETextureCreateFlag::UAV, false, false };
    Info.ResourceName = L"Cubemap";
    auto CubeMap = jRenderTargetPool::GetRenderTarget(Info);
    {
        DEBUG_EVENT(InRenderFrameContextPtr, "DrawCubemap");

#if USE_RESOURCE_BARRIER_BATCHER
        InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(InTwoMirrorBallSphereMap, EResourceLayout::SHADER_READ_ONLY);
        InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(CubeMap->GetTexture(), EResourceLayout::UAV);
#else
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTwoMirrorBallSphereMap, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), CubeMap->GetTexture(), EResourceLayout::UAV);
#endif // USE_RESOURCE_BARRIER_BATCHER

        struct jMipUniformBuffer
        {
            int32 Width;
            int32 Height;
            int32 mip;
            int32 maxMip;
        };
        jMipUniformBuffer MipUBO;
        MipUBO.maxMip = jTexture::GetMipLevels(CubeMap->Info.Width, CubeMap->Info.Height);

        for (int32 i = 0; i < MipUBO.maxMip; ++i)
        {
            MipUBO.Width = CubeMap->Info.Width >> i;
            MipUBO.Height = CubeMap->Info.Height >> i;
            MipUBO.mip = i;

            std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
            int32 BindingPoint = 0;
            jShaderBindingArray ShaderBindingArray;
            jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

            // Binding 0
            if (ensure(InTwoMirrorBallSphereMap))
            {
                ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jTextureResource>(InTwoMirrorBallSphereMap, nullptr)));
            }

            // Binding 1
            if (ensure(CubeMap && CubeMap->GetTexture()))
            {
                ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jTextureResource>(CubeMap->GetTexture(), nullptr, i)));
            }

            // Binding 2
            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
                g_rhi->CreateUniformBufferBlock(jNameStatic("MipUniformBuffer"), jLifeTimeType::OneFrame, sizeof(MipUBO)));
            OneFrameUniformBuffer->UpdateBufferData(&MipUBO, sizeof(MipUBO));
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

            CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("GenCubemapFromSphericalProbe"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/gencubemapfromsphericalprobe_cs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            static jShader* Shader = g_rhi->CreateShader(shaderInfo);

            jShaderBindingLayoutArray ShaderBindingLayoutArray;
            ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

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
            g_rhi->DispatchCompute(InRenderFrameContextPtr
                , MipUBO.Width / 16 + ((MipUBO.Width % 16) ? 1 : 0)
                , MipUBO.Height / 16 + ((MipUBO.Height % 16) ? 1 : 0)
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
    DirectX::CaptureTexture(g_rhi_dx12->CommandBufferManager->GetCommandQueue().Get()
        , texture_dx12->Texture->Get(), true, image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const std::wstring DestFilePath = ConvertToWchar(InDestFilePath);

    // Save image
    DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DestFilePath.c_str());

#if USE_RESOURCE_BARRIER_BATCHER
    InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(CubeMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
#else
    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), CubeMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
#endif // USE_RESOURCE_BARRIER_BATCHER

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

    static jRenderTargetInfo Info = { ETextureType::TEXTURE_CUBE, ETextureFormat::RGBA16F, InDestTextureSize.x, InDestTextureSize.y
        , 6, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f), ETextureCreateFlag::RTV | ETextureCreateFlag::UAV, false, false };
    Info.ResourceName = L"IrradianceMap";
    auto IrradianceMap = jRenderTargetPool::GetRenderTarget(Info);
    {
        DEBUG_EVENT(InRenderFrameContextPtr, "GenIrradianceMap");

#if USE_RESOURCE_BARRIER_BATCHER
        InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(InCubemap, EResourceLayout::SHADER_READ_ONLY);
        InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(IrradianceMap->GetTexture(), EResourceLayout::UAV);
#else
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InCubemap, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), IrradianceMap->GetTexture(), EResourceLayout::UAV);
#endif // USE_RESOURCE_BARRIER_BATCHER

        struct jRTSizeUniformBuffer
        {
            int32 Width;
            int32 Height;
        };
        jRTSizeUniformBuffer RTSizeUBO;
        RTSizeUBO.Width = Info.Width;
        RTSizeUBO.Height = Info.Height;

        std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        // Binding 0
        if (ensure(InCubemap))
        {
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(InCubemap, nullptr)));
        }

        // Binding 1
        if (ensure(IrradianceMap && IrradianceMap->GetTexture()))
        {
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(IrradianceMap->GetTexture(), nullptr)));
        }

        // Binding 2
        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("MipUniformBuffer"), jLifeTimeType::OneFrame, sizeof(RTSizeUBO)));
        OneFrameUniformBuffer->UpdateBufferData(&RTSizeUBO, sizeof(RTSizeUBO));
        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
            , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

        CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("GenIrradianceMap"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/genirradiancemap_cs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        static jShader* Shader = g_rhi->CreateShader(shaderInfo);

        jShaderBindingLayoutArray ShaderBindingLayoutArray;
        ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

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

        int32 X = (Info.Width / 16) + ((Info.Width % 16) ? 1 : 0);
        int32 Y = (Info.Height / 16) + ((Info.Height % 16) ? 1 : 0);
        g_rhi->DispatchCompute(InRenderFrameContextPtr, X, Y, 6);
    }

    // Flush all render command
    InRenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::BasePass);
    InRenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
    g_rhi->Flush();

    // Capture image form texture
    DirectX::ScratchImage image;
    jTexture_DX12* texture_dx12 = (jTexture_DX12*)IrradianceMap->GetTexture();
    DirectX::CaptureTexture(g_rhi_dx12->CommandBufferManager->GetCommandQueue().Get()
        , texture_dx12->Texture->Get(), true, image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const std::wstring DestFilePath = ConvertToWchar(InDestFilePath);

    // Save image
    DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DestFilePath.c_str());

#if USE_RESOURCE_BARRIER_BATCHER
    InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(IrradianceMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
#else
    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), IrradianceMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
#endif // USE_RESOURCE_BARRIER_BATCHER

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

    static jRenderTargetInfo Info = { ETextureType::TEXTURE_CUBE, ETextureFormat::RGBA16F, InDestTextureSize.x, InDestTextureSize.y
        , 6, true, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f), ETextureCreateFlag::RTV | ETextureCreateFlag::UAV, false, false };
    Info.ResourceName = L"FilteredEnvMap";
    auto FilteredEnvMap = jRenderTargetPool::GetRenderTarget(Info);
    {
        DEBUG_EVENT(InRenderFrameContextPtr, "GenFilteredEnvMap");

#if USE_RESOURCE_BARRIER_BATCHER
        InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(InCubemap, EResourceLayout::SHADER_READ_ONLY);
        InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(FilteredEnvMap->GetTexture(), EResourceLayout::UAV);
#else
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InCubemap, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), FilteredEnvMap->GetTexture(), EResourceLayout::UAV);
#endif // USE_RESOURCE_BARRIER_BATCHER

        struct jMipUniformBuffer
        {
            int32 Width;
            int32 Height;
            int32 mip;
            int32 maxMip;
        };
        jMipUniformBuffer MipUBO;
        MipUBO.maxMip = jTexture::GetMipLevels(FilteredEnvMap->Info.Width, FilteredEnvMap->Info.Height);

        for (int32 i = 0; i < MipUBO.maxMip; ++i)
        {
            MipUBO.Width = FilteredEnvMap->Info.Width >> i;
            MipUBO.Height = FilteredEnvMap->Info.Height >> i;
            MipUBO.mip = i;

            std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
            int32 BindingPoint = 0;
            jShaderBindingArray ShaderBindingArray;
            jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

            // Binding 0
            if (ensure(InCubemap))
            {
                ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jTextureResource>(InCubemap, nullptr)));
            }

            // Binding 1
            if (ensure(FilteredEnvMap && FilteredEnvMap->GetTexture()))
            {
                ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jTextureResource>(FilteredEnvMap->GetTexture(), nullptr, i)));
            }

            // Binding 2
            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("MipUniformBuffer"), jLifeTimeType::OneFrame, sizeof(MipUBO)));
            OneFrameUniformBuffer->UpdateBufferData(&MipUBO, sizeof(MipUBO));
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

            CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("GenFilteredEnvMap"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/genprefilteredenvmap_cs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            static jShader* Shader = g_rhi->CreateShader(shaderInfo);

            jShaderBindingLayoutArray ShaderBindingLayoutArray;
            ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

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

            int32 X = (MipUBO.Width / 16) + ((MipUBO.Width % 16) ? 1 : 0);
            int32 Y = (MipUBO.Height / 16) + ((MipUBO.Height % 16) ? 1 : 0);
            g_rhi->DispatchCompute(InRenderFrameContextPtr, X, Y, 6);
        }
    }

    // Flush all render command
    InRenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::BasePass);
    InRenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
    g_rhi->Flush();

    // Capture from texture
    DirectX::ScratchImage image;
    jTexture_DX12* texture_dx12 = (jTexture_DX12*)FilteredEnvMap->GetTexture();
    DirectX::CaptureTexture(g_rhi_dx12->CommandBufferManager->GetCommandQueue().Get()
        , texture_dx12->Texture->Get(), true, image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const std::wstring DestFilePath = ConvertToWchar(InDestFilePath);

    // Save image
    DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DestFilePath.c_str());

#if USE_RESOURCE_BARRIER_BATCHER
    InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(FilteredEnvMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
#else
    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), FilteredEnvMap->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
#endif // USE_RESOURCE_BARRIER_BATCHER

    return FilteredEnvMap;
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

#if USE_RESOURCE_BARRIER_BATCHER
    InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(RenderTarget, EResourceLayout::UAV);
#else
	g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), RenderTarget, EResourceLayout::UAV);
#endif // USE_RESOURCE_BARRIER_BATCHER

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

	const int32 X = (Width / 16) + ((Width % 16) ? 1 : 0);
	const int32 Y = (Height / 16) + ((Height % 16) ? 1 : 0);
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

#if USE_RESOURCE_BARRIER_BATCHER
    InRenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->AddTransition(InRenderTargetPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
#else
	g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTargetPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
#endif // USE_RESOURCE_BARRIER_BATCHER

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
	jAttachment color = jAttachment(InRenderTargetPtr, EAttachmentLoadStoreOp::LOAD_STORE
		, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, InRenderTargetPtr->GetLayout(), EResourceLayout::COLOR_ATTACHMENT);
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
		jShaderInfo shaderInfo;
		shaderInfo.SetName(jNameStatic("DrawQuadVS"));
		shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"));
		shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
		Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

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