#include "pch.h"
#include "jRenderer.h"
#include "Scene/Light/jDirectionalLight.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "jOptions.h"
#include "RHI/Vulkan/jUniformBufferBlock_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"
#include "jPrimitiveUtil.h"
#include "Scene/jRenderObject.h"
#include "RHI/jShaderBindingLayout.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "jDirectionalLightDrawCommandGenerator.h"
#include "jPointLightDrawCommandGenerator.h"
#include "jSpotLightDrawCommandGenerator.h"
#include "RHI/jRenderTargetPool.h"
#include "Material/jMaterial.h"
#include "RHI/Vulkan/jRenderFrameContext_Vulkan.h"
#include "RHI/DX12/jRHI_DX12.h"
#include "RHI/DX12/jSwapchain_DX12.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/DX12/jBufferUtil_DX12.h"
#include "FileLoader/jImageFileLoader.h"
#include "RHI/DX12/jTexture_DX12.h"
#include "RHI/jRHIUtil.h"
#include "RHI/DX12/jShader_DX12.h"
#include "dxcapi.h"
#include "jGame.h"
#include "RHI/DX12/jVertexBuffer_DX12.h"
#include "RHI/DX12/jIndexBuffer_DX12.h"

jTexture_DX12* jRenderer::m_raytracingOutput;
ComPtr<ID3D12RootSignature> jRenderer::m_raytracingGlobalRootSignature;
ComPtr<ID3D12RootSignature> jRenderer::m_raytracingLocalRootSignature;
ComPtr<ID3D12RootSignature> jRenderer::m_raytracingEmptyLocalRootSignature;

#define ASYNC_WITH_SETUP 1
#define PARALLELFOR_WITH_PASSSETUP 1

struct jSimplePushConstant
{
    Vector4 Color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

    bool ShowVRSArea = false;
    bool Padding[3];

    bool ShowGrid = true;
    bool Padding2[3];
};

void jRenderer::Setup()
{
    SCOPE_CPU_PROFILE(Setup);

    FrameIndex = g_rhi->GetCurrentFrameIndex();
    UseForwardRenderer = RenderFrameContextPtr->UseForwardRenderer;

    View.ShadowCasterLights.reserve(View.Lights.size());

    // View 별로 저장 할 수 있어야 함
    for (int32 i = 0; i < View.Lights.size(); ++i)
    {
        jViewLight& ViewLight = View.Lights[i];

        if (ViewLight.Light)
        {
            if (ViewLight.Light->IsShadowCaster)
            {
                ViewLight.ShadowMapPtr = RenderFrameContextPtr->SceneRenderTargetPtr->GetShadowMap(ViewLight.Light);
            }

            ViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(ViewLight.ShadowMapPtr ? ViewLight.ShadowMapPtr->GetTexture() : nullptr);

            if (ViewLight.Light->IsShadowCaster)
            {
                jViewLight NewViewLight = ViewLight;
                NewViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(nullptr);
                View.ShadowCasterLights.push_back(NewViewLight);
            }
        }       
    }

#if ASYNC_WITH_SETUP
    ShadowPassSetupCompleteEvent = std::async(std::launch::async, &jRenderer::SetupShadowPass, this);
#else
    jRenderer::SetupShadowPass();
    jRenderer::SetupBasePass();
#endif
}

void jRenderer::SetupShadowPass()
{
    SCOPE_CPU_PROFILE(SetupShadowPass);

    ShadowDrawInfo.reserve(View.Lights.size());

    for (int32 i = 0; i < View.ShadowCasterLights.size(); ++i)
    {
        jViewLight& ViewLight = View.ShadowCasterLights[i];
        if (!ViewLight.Light->IsShadowCaster)
            continue;

        ShadowDrawInfo.push_back(jShadowDrawInfo());
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[ShadowDrawInfo.size() - 1];

        const bool IsUseReverseZShadow = USE_REVERSEZ_PERSPECTIVE_SHADOW && (ViewLight.Light->IsUseRevereZPerspective());

        // Prepare shadowpass pipeline
        auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f
            , false, false, EMSAASamples::COUNT_1, true, 0.2f, false, false>::Create();
        auto DepthStencilState = IsUseReverseZShadow ? TDepthStencilStateInfo<true, true, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create()
            : TDepthStencilStateInfo<true, true, ECompareOp::LEQUAL, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE
            , EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

        const int32 RTWidth = ViewLight.ShadowMapPtr->Info.Width;
        const int32 RTHeight = ViewLight.ShadowMapPtr->Info.Height;

        jPipelineStateFixedInfo ShadpwPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), false);

        {            
            const jRTClearValue ClearDepth = IsUseReverseZShadow ? jRTClearValue(0.0f, 0) : jRTClearValue(1.0f, 0);

            jAttachment depth = jAttachment(ViewLight.ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE
                , ClearDepth, EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);

            // Setup attachment
            jRenderPassInfo renderPassInfo;
            renderPassInfo.Attachments.push_back(depth);

            // Setup subpass of ShadowPass
            jSubpass subpass;
            subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
            subpass.OutputDepthAttachment = 0;
            renderPassInfo.Subpasses.push_back(subpass);

            ShadowPasses.ShadowMapRenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { RTWidth, RTHeight });
        }

        // Shadow 기준 View 를 생성, 현재 Light 가 가진 Shadow Camera 와 ViewLight를 설정해줌
        ShadowPasses.ViewLight = ViewLight;

        jGraphicsPipelineShader ShadowShader;
        {
            jShaderInfo shaderInfo;

            if (ViewLight.Light->IsOmnidirectional())
            {
                shaderInfo.SetName(jNameStatic("shadow_testVS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/omni_shadow_vs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                ShadowShader.VertexShader = g_rhi->CreateShader(shaderInfo);

                shaderInfo.SetName(jNameStatic("shadow_testPS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/omni_shadow_fs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                ShadowShader.PixelShader = g_rhi->CreateShader(shaderInfo);
            }
            else
            {
                if (ViewLight.Light->Type == ELightType::SPOT)
                {
                    shaderInfo.SetName(jNameStatic("shadow_testVS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/spot_shadow_vs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                    ShadowShader.VertexShader = g_rhi->CreateShader(shaderInfo);

                    shaderInfo.SetName(jNameStatic("shadow_testPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/spot_shadow_fs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    ShadowShader.PixelShader = g_rhi->CreateShader(shaderInfo);
                }
                else
                {
                    shaderInfo.SetName(jNameStatic("shadow_testVS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/shadow_vs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                    ShadowShader.VertexShader = g_rhi->CreateShader(shaderInfo);

                    shaderInfo.SetName(jNameStatic("shadow_testPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/shadow_fs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    ShadowShader.PixelShader = g_rhi->CreateShader(shaderInfo);
                }
            }
        }
        jGraphicsPipelineShader ShadowInstancingShader;
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("shadow_testVS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shadow_instancing_vs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
            ShadowInstancingShader.VertexShader = g_rhi->CreateShader(shaderInfo);

            shaderInfo.SetName(jNameStatic("shadow_testGS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shadow_fs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            ShadowInstancingShader.PixelShader = g_rhi->CreateShader(shaderInfo);
        }

#if PARALLELFOR_WITH_PASSSETUP
        ShadowPasses.DrawCommands.resize(jObject::GetShadowCasterRenderObject().size());
        jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetShadowCasterRenderObject()
            , [&](size_t InIndex, jRenderObject* InRenderObject)
            {
                jMaterial* Material = nullptr;

                // todo : Masked material need to set Material for jDrawCommand
                // iter->MaterialPtr;

                const bool ShouldUseOnePassPointLightShadow = (ViewLight.Light->Type == ELightType::POINT);
                const jVertexBuffer* OverrideInstanceData = (ShouldUseOnePassPointLightShadow ? jRHI::CubeMapInstanceDataForSixFace : nullptr);

                new (&ShadowPasses.DrawCommands[InIndex]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, InRenderObject, ShadowPasses.ShadowMapRenderPass
                    , (InRenderObject->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, Material, {}, nullptr, OverrideInstanceData);
                ShadowPasses.DrawCommands[InIndex].PrepareToDraw(true);
            });
#else
        ShadowPasses.DrawCommands.resize(jObject::GetShadowCasterRenderObject().size());
        {
            int32 i = 0;
            for (auto iter : jObject::GetShadowCasterRenderObject())
            {
                jMaterial* Material = nullptr;

                // todo : Masked material need to set Material for jDrawCommand
                // iter->MaterialPtr;

                const bool ShouldUseOnePassPointLightShadow = (ViewLight.Light->Type == ELightType::POINT);
                const jVertexBuffer* OverrideInstanceData = (ShouldUseOnePassPointLightShadow ? jRHI::CubeMapInstanceDataForSixFace : nullptr);

                new (&ShadowPasses.DrawCommands[i]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, iter, ShadowPasses.ShadowMapRenderPass
                    , (iter->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, Material, {}, nullptr, OverrideInstanceData);
                ShadowPasses.DrawCommands[i].PrepareToDraw(true);
                ++i;
            }
        }
#endif
    }
}

void jRenderer::SetupBasePass()
{
    SCOPE_CPU_PROFILE(SetupBasePass);

    // Prepare basepass pipeline
    jRasterizationStateInfo* RasterizationState = nullptr;
    switch (g_rhi->GetSelectedMSAASamples())
    {
    case EMSAASamples::COUNT_1:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_2:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_4:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_8:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
        break;
    default:
        check(0);
        break;
    }
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo BasePassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    auto TranslucentBlendingState = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    jPipelineStateFixedInfo TranslucentPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, TranslucentBlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr, EAttachmentLoadStoreOp::CLEAR_STORE
        , EAttachmentLoadStoreOp::CLEAR_STORE, ClearDepth
        , EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        {
            resolve = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT, true);
        }
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    if (!UseForwardRenderer)
    {
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
        {
            jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i], EAttachmentLoadStoreOp::CLEAR_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);
        }
    }

    const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

    if (UseForwardRenderer || gOptions.UseSubpass)
    {
        jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
            , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
        renderPassInfo.Attachments.push_back(color);
    }

    const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
    renderPassInfo.Attachments.push_back(depth);

    const int32 ResolveAttachemntIndex = (int32)renderPassInfo.Attachments.size();
    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            renderPassInfo.Attachments.push_back(resolve);
    }

    //////////////////////////////////////////////////////////////////////////
    // Setup subpass of BasePass
    {
        // First subpass, Geometry pass
        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);

        if (UseForwardRenderer)
        {
            subpass.OutputColorAttachments.push_back(0);
        }
        else
        {
            const int32 GBufferCount = LightPassAttachmentIndex;
            for (int32 i = 0; i < GBufferCount; ++i)
            {
                subpass.OutputColorAttachments.push_back(i);
            }
        }

        subpass.OutputDepthAttachment = DepthAttachmentIndex;
        if (UseForwardRenderer)
        {
            if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
                subpass.OutputResolveAttachment = ResolveAttachemntIndex;
        }
        renderPassInfo.Subpasses.push_back(subpass);
    }
    if (!UseForwardRenderer && gOptions.UseSubpass)
    {
        // Second subpass, Lighting pass
        jSubpass subpass;
        subpass.Initialize(1, 2, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);

        const int32 GBufferCount = LightPassAttachmentIndex;
        for (int32 i = 0; i < GBufferCount; ++i)
        {
            subpass.InputAttachments.push_back(i);
        }

        subpass.OutputColorAttachments.push_back(LightPassAttachmentIndex);
        subpass.OutputDepthAttachment = DepthAttachmentIndex;

        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            subpass.OutputResolveAttachment = ResolveAttachemntIndex;

        renderPassInfo.Subpasses.push_back(subpass);
    }
    //////////////////////////////////////////////////////////////////////////
    BaseRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

    auto GetOrCreateShaderFunc = [UseForwardRenderer = this->UseForwardRenderer](const jRenderObject* InRenderObject)
    {
        jGraphicsPipelineShader Shaders;
        jShaderInfo shaderInfo;

        if (InRenderObject->HasInstancing())
        {
            check(UseForwardRenderer);

            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("default_instancing_testVS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_instancing_vs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
            Shaders.VertexShader = g_rhi->CreateShader(shaderInfo);

            shaderInfo.SetName(jNameStatic("default_instancing_testPS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_fs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            Shaders.PixelShader = g_rhi->CreateShader(shaderInfo);
            return Shaders;
        }

        if (UseForwardRenderer)
        {
            shaderInfo.SetName(jNameStatic("default_testVS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_vs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
            Shaders.VertexShader = g_rhi->CreateShader(shaderInfo);

            jShaderForwardPixelShader::ShaderPermutation ShaderPermutation;
            ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
            ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_REVERSEZ>(USE_REVERSEZ_PERSPECTIVE_SHADOW);
            Shaders.PixelShader = jShaderForwardPixelShader::CreateShader(ShaderPermutation);
        }
        else
        {
            const bool IsUseSphericalMap = InRenderObject->MaterialPtr && InRenderObject->MaterialPtr->IsUseSphericalMap();
            const bool HasAlbedoTexture = InRenderObject->MaterialPtr && InRenderObject->MaterialPtr->HasAlbedoTexture();
            const bool IsUseSRGBAlbedoTexture = InRenderObject->MaterialPtr && InRenderObject->MaterialPtr->IsUseSRGBAlbedoTexture();
            const bool HasVertexColor = InRenderObject->GeometryDataPtr && InRenderObject->GeometryDataPtr->HasVertexColor();
            const bool HasVertexBiTangent = InRenderObject->GeometryDataPtr && InRenderObject->GeometryDataPtr->HasVertexBiTangent();

            jShaderGBufferVertexShader::ShaderPermutation ShaderPermutationVS;
            ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_COLOR>(HasVertexColor);
            ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_BITANGENT>(HasVertexBiTangent);
            ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_ALBEDO_TEXTURE>(HasAlbedoTexture);
            ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_SPHERICAL_MAP>(IsUseSphericalMap);
            Shaders.VertexShader = jShaderGBufferVertexShader::CreateShader(ShaderPermutationVS);

            jShaderGBufferPixelShader::ShaderPermutation ShaderPermutationPS;
            ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_VERTEX_COLOR>(HasVertexColor);
            ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_ALBEDO_TEXTURE>(HasAlbedoTexture);
            ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_SRGB_ALBEDO_TEXTURE>(IsUseSRGBAlbedoTexture);
            ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
            ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_PBR>(ENABLE_PBR);
            Shaders.PixelShader = jShaderGBufferPixelShader::CreateShader(ShaderPermutationPS);
        }
        return Shaders;
    };

    jShaderInfo shaderInfo;
    jGraphicsPipelineShader TranslucentPassShader;
    {
        shaderInfo.SetName(jNameStatic("default_testVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/gbuffer_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        TranslucentPassShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        jShaderGBufferPixelShader::ShaderPermutation ShaderPermutation;
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_VERTEX_COLOR>(0);
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_ALBEDO_TEXTURE>(1);
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
        TranslucentPassShader.PixelShader = jShaderGBufferPixelShader::CreateShader(ShaderPermutation);
    }

    jSimplePushConstant SimplePushConstantData;
    SimplePushConstantData.ShowVRSArea = gOptions.ShowVRSArea;
    SimplePushConstantData.ShowGrid = gOptions.ShowGrid;

    jPushConstant* SimplePushConstant = new(jMemStack::Get()->Alloc<jPushConstant>()) jPushConstant(SimplePushConstantData, EShaderAccessStageFlag::FRAGMENT);

#if PARALLELFOR_WITH_PASSSETUP
    BasePasses.resize(jObject::GetStaticRenderObject().size());
    jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetStaticRenderObject()
        , [&](size_t InIndex, jRenderObject* InRenderObject)
    {
        jMaterial* Material = nullptr;
        if (InRenderObject->MaterialPtr)
        {
            Material = InRenderObject->MaterialPtr.get();
        }
        else
        {
            if (GDefaultMaterial)
            {
                Material = GDefaultMaterial;
            }
        }

        new (&BasePasses[InIndex]) jDrawCommand(RenderFrameContextPtr, &View, InRenderObject, BaseRenderPass
            , GetOrCreateShaderFunc(InRenderObject), &BasePassPipelineStateFixed, Material, {}, SimplePushConstant);
        BasePasses[InIndex].PrepareToDraw(false);
    });
#else
    BasePasses.resize(jObject::GetStaticRenderObject().size());
    int32 i = 0;
    for (auto iter : jObject::GetStaticRenderObject())
    {
        jMaterial* Material = nullptr;
        if (iter->MaterialPtr)
        {
            Material = iter->MaterialPtr.get();
        }
        else
        {
            if (GDefaultMaterial)
            {
                Material = GDefaultMaterial;
            }
        }

        new (&BasePasses[i]) jDrawCommand(RenderFrameContextPtr, &View, iter, BaseRenderPass
            , GetOrCreateShaderFunc(iter), &BasePassPipelineStateFixed, Material, {}, SimplePushConstant);
        BasePasses[i].PrepareToDraw(false);
        ++i;
    }
#endif
}

void jRenderer::ShadowPass()
{
#if ASYNC_WITH_SETUP
    if (ShadowPassSetupCompleteEvent.valid())
        ShadowPassSetupCompleteEvent.wait();

    BasePassSetupCompleteEvent = std::async(std::launch::async, &jRenderer::SetupBasePass, this);
#endif

    SCOPE_CPU_PROFILE(ShadowPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, ShadowPass);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "ShadowPass", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

    for (int32 i = 0; i < ShadowDrawInfo.size(); ++i)
    {
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[i];

        const char* ShadowPassEventName = nullptr;
        switch (ShadowPasses.ViewLight.Light->Type)
        {
        case ELightType::DIRECTIONAL:
            ShadowPassEventName = "DirectionalLight";
            break;
        case ELightType::POINT:
            ShadowPassEventName = "PointLight";
            break;
        case ELightType::SPOT:
            ShadowPassEventName = "SpotLight";
            break;
        }
        
        DEBUG_EVENT(RenderFrameContextPtr, ShadowPassEventName);

        const std::shared_ptr<jRenderTarget>& ShadowMapPtr = ShadowPasses.GetShadowMapPtr();

        {
            auto NewLayout = ShadowMapPtr->GetTexture()->IsDepthOnlyFormat() ? EImageLayout::DEPTH_ATTACHMENT : EImageLayout::DEPTH_STENCIL_ATTACHMENT;
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), NewLayout);
        }

        if (ShadowPasses.ShadowMapRenderPass && ShadowPasses.ShadowMapRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            //ShadowpassOcclusionTest.BeginQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
            for (const auto& command : ShadowPasses.DrawCommands)
            {
                command.Draw();
            }
            //ShadowpassOcclusionTest.EndQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
            ShadowPasses.ShadowMapRenderPass->EndRenderPass();
        }

        {
            auto NewLayout = ShadowMapPtr->GetTexture()->IsDepthOnlyFormat() ? EImageLayout::DEPTH_READ_ONLY : EImageLayout::DEPTH_STENCIL_READ_ONLY;
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), NewLayout);
        }
    }
}

void jRenderer::BasePass()
{
    if (BasePassSetupCompleteEvent.valid())
        BasePassSetupCompleteEvent.wait();

    {
        SCOPE_CPU_PROFILE(BasePass);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, BasePass);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "BasePass", Vector4(0.8f, 0.8f, 0.0f, 1.0f));

        if (UseForwardRenderer)
        {
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
        }
        else
        {
            for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
            {
                g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i]->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
            }
        }

        {
            auto NewLayout = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture()->IsDepthOnlyFormat() ? EImageLayout::DEPTH_ATTACHMENT : EImageLayout::DEPTH_STENCIL_ATTACHMENT;
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), NewLayout);
        }

        //BasepassOcclusionTest.BeginQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
        if (BaseRenderPass && BaseRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            // Draw G-Buffer : subpass 0 
            for (const auto& command : BasePasses)
            {
                command.Draw();
            }

            // Draw Light : subpass 1
            if (!UseForwardRenderer && gOptions.UseSubpass)
            {
                g_rhi->NextSubpass(RenderFrameContextPtr->GetActiveCommandBuffer());
                DeferredLightPass_TodoRefactoring(BaseRenderPass);
            }

            BaseRenderPass->EndRenderPass();
        }

        if (!UseForwardRenderer && !gOptions.UseSubpass)
        {
            DeferredLightPass_TodoRefactoring(BaseRenderPass);
        }
        //BasepassOcclusionTest.EndQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
    }

    static bool EnableAtmosphericShadowing = true;
    if (EnableAtmosphericShadowing)
    {
        jDirectionalLight* DirectionalLight = nullptr;
        for (auto light : jLight::GetLights())
        {
            if (light->Type == ELightType::DIRECTIONAL)
            {
                DirectionalLight = (jDirectionalLight*)light;
                break;
            }
        }
        check(DirectionalLight);

        jCamera* MainCamera = jCamera::GetMainCamera();
        check(MainCamera);

        SCOPE_CPU_PROFILE(AtmosphericShadowing);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, AtmosphericShadowing);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "AtmosphericShadowing", Vector4(0.8f, 0.8f, 0.8f, 1.0f));

        std::shared_ptr<jRenderTarget> AtmosphericShadowing = RenderFrameContextPtr->SceneRenderTargetPtr->AtmosphericShadowing;
        int32 Width = AtmosphericShadowing->Info.Width;
        int32 Height = AtmosphericShadowing->Info.Height;

        struct jAtmosphericData
        {
            Matrix ShadowVP;
            Matrix VP;
            Vector CameraPos;
            float CameraNear;
            Vector LightCameraDirection;
            float CameraFar;
            float AnisoG;
            float SlopeOfDist;
            float InScatteringLambda;
            float Dummy;
            int TravelCount;
            int RTWidth;
            int RTHeight;
            int UseNoise;  // todo : change to #define USE_NOISE
        };

        auto LightCamera = DirectionalLight->GetLightCamra();
        auto ShadowVP = DirectionalLight->GetLightData().ShadowVP;
        auto LightCameraDirection = LightCamera->GetForwardVector();

        jAtmosphericData AtmosphericData;
        AtmosphericData.ShadowVP = ShadowVP;
        AtmosphericData.VP = MainCamera->GetViewProjectionMatrix();
        AtmosphericData.CameraPos = MainCamera->Pos;
        AtmosphericData.LightCameraDirection = LightCameraDirection;
        AtmosphericData.CameraFar = MainCamera->Far;
        AtmosphericData.CameraNear = MainCamera->Near;
        AtmosphericData.AnisoG = gOptions.AnisoG;
        AtmosphericData.SlopeOfDist = 0.25f;
        AtmosphericData.InScatteringLambda = 0.001f;
        AtmosphericData.TravelCount = 64;
        AtmosphericData.RTWidth = Width;
        AtmosphericData.RTHeight = Height;
        AtmosphericData.UseNoise = true;

        std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        auto ShadowMapTexture = RenderFrameContextPtr->SceneRenderTargetPtr->GetShadowMap(DirectionalLight)->GetTexture();
        check(ShadowMapTexture);

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);
        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapTexture, EImageLayout::SHADER_READ_ONLY);
        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EImageLayout::UAV);

        // Binding 0
        {
            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[0]->GetTexture(), nullptr));
        }

        // Binding 1
        {
            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), nullptr));
        }

        // Binding 2
        {
            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(ShadowMapTexture, nullptr));
        }

        // Binding 3        
        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("AtmosphericDataUniformBuffer"), jLifeTimeType::OneFrame, sizeof(AtmosphericData)));
        OneFrameUniformBuffer->UpdateBufferData(&AtmosphericData, sizeof(AtmosphericData));
        ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
            , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true);

        // Binding 4
        {
            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(AtmosphericShadowing->GetTexture(), nullptr));
        }

        CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("GenIrradianceMap"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AtmosphericShadowing_cs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        static jShader* Shader = g_rhi->CreateShader(shaderInfo);

        jShaderBindingLayoutArray ShaderBindingLayoutArray;
        ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

        jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

        computePipelineStateInfo->Bind(RenderFrameContextPtr);

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

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), computePipelineStateInfo, ShaderBindingInstanceCombiner, 0);

        int32 X = (Width / 16) + ((Width % 16) ? 1 : 0);
        int32 Y = (Height / 16) + ((Height % 16) ? 1 : 0);
        g_rhi->DispatchCompute(RenderFrameContextPtr, X, Y, 1);

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EImageLayout::SHADER_READ_ONLY);
    }

    if (EnableAtmosphericShadowing)
    {
        static jFullscreenQuadPrimitive* GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
        std::shared_ptr<jRenderTarget> AtmosphericShadowing = RenderFrameContextPtr->SceneRenderTargetPtr->AtmosphericShadowing;

        auto RT = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr;
        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RT->GetTexture(), EImageLayout::COLOR_ATTACHMENT);

        jRasterizationStateInfo* RasterizationState = nullptr;
        switch (g_rhi->GetSelectedMSAASamples())
        {
        case EMSAASamples::COUNT_1:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_2:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_4:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_8:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
            break;
        default:
            check(0);
            break;
        }
        auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

        const int32 RTWidth = RT->Info.Width;
        const int32 RTHeight = RT->Info.Height;

        // Create fixed pipeline states
        jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

        const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
        const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

        jRenderPassInfo renderPassInfo;
        jAttachment color = jAttachment(RT, EAttachmentLoadStoreOp::LOAD_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
        renderPassInfo.Attachments.push_back(color);

        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
        subpass.OutputColorAttachments.push_back(0);
        renderPassInfo.Subpasses.push_back(subpass);

        auto RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

        ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
            , ResourceInlineAllactor.Alloc<jTextureResource>(AtmosphericShadowing->GetTexture(), SamplerState));

        std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
        jShaderBindingInstanceArray ShaderBindingInstanceArray;
        ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());

        jGraphicsPipelineShader Shader;
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("AtmosphericShadowingApplyVS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
            Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

            shaderInfo.SetName(jNameStatic("AtmosphericShadowingApplyPS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AtmosphericShadowingApply_ps.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
        }

        jDrawCommand DrawCommand(RenderFrameContextPtr, GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
            , Shader, &PostProcessPassPipelineStateFixed, GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
        DrawCommand.Test = true;
        DrawCommand.PrepareToDraw(false);

        if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            DrawCommand.Draw();
            RenderPass->EndRenderPass();
        }
    }
}

void jRenderer::DeferredLightPass_TodoRefactoring(jRenderPass* InRenderPass)
{
    SCOPE_CPU_PROFILE(LightingPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, LightingPass);
    DEBUG_EVENT(RenderFrameContextPtr, "LightingPass");

    if (!gOptions.UseSubpass)
    {
        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
        {
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i]->GetTexture(), EImageLayout::SHADER_READ_ONLY);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // GBuffer Input attachment 추가
    std::shared_ptr<jShaderBindingInstance> GBufferShaderBindingInstance
        = RenderFrameContextPtr->SceneRenderTargetPtr->PrepareGBufferShaderBindingInstance(gOptions.UseSubpass);

    jShaderBindingInstanceArray DefaultLightPassShaderBindingInstances;
    DefaultLightPassShaderBindingInstances.Add(GBufferShaderBindingInstance.get());
    DefaultLightPassShaderBindingInstances.Add(View.ViewUniformBufferShaderBindingInstance.get());
    //////////////////////////////////////////////////////////////////////////

    std::vector<jDrawCommand> LightPasses;
    LightPasses.resize(View.Lights.size());

    const int32 RTWidth = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Width;
    const int32 RTHeight = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Height;
    jDirectionalLightDrawCommandGenerator DirectionalLightPass(DefaultLightPassShaderBindingInstances);
    DirectionalLightPass.Initialize(RTWidth, RTHeight);

    jPointLightDrawCommandGenerator PointLightPass(DefaultLightPassShaderBindingInstances);
    PointLightPass.Initialize(RTWidth, RTHeight);

    jSpotLightDrawCommandGenerator SpotLightPass(DefaultLightPassShaderBindingInstances);
    SpotLightPass.Initialize(RTWidth, RTHeight);

    jDrawCommandGenerator* LightDrawCommandGenerator[(int32)ELightType::MAX] = { 0, };
    LightDrawCommandGenerator[(int32)ELightType::DIRECTIONAL] = &DirectionalLightPass;
    LightDrawCommandGenerator[(int32)ELightType::POINT] = &PointLightPass;
    LightDrawCommandGenerator[(int32)ELightType::SPOT] = &SpotLightPass;

    {
        const int32 SubpassIndex = (!UseForwardRenderer && gOptions.UseSubpass) ? 1 : 0;

        if (!gOptions.UseSubpass)
        {
            const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
            const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

            jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr, EAttachmentLoadStoreOp::LOAD_DONTCARE
                , EAttachmentLoadStoreOp::LOAD_STORE, ClearDepth
                , RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetLayout(), EImageLayout::DEPTH_STENCIL_READ_ONLY);

            // Setup attachment
            jRenderPassInfo renderPassInfo;
            jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetLayout(), EImageLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);

            const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
            renderPassInfo.Attachments.push_back(depth);

            // Setup subpass of LightingPass
            jSubpass subpass;
            subpass.SourceSubpassIndex = 0;
            subpass.DestSubpassIndex = 1;

            for (int32 i = 0; i < DepthAttachmentIndex; ++i)
            {
                subpass.OutputColorAttachments.push_back(i);
            }

            subpass.OutputDepthAttachment = DepthAttachmentIndex;
            subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
            renderPassInfo.Subpasses.push_back(subpass);

            InRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

            check(InRenderPass);
            InRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer());
        }

        check(View.Lights.size() == LightPasses.size());
        for (int32 i = 0; i < (int32)LightPasses.size(); ++i)
        {
            LightDrawCommandGenerator[(int32)View.Lights[i].Light->Type]->GenerateDrawCommand(&LightPasses[i], RenderFrameContextPtr, &View, View.Lights[i], InRenderPass, SubpassIndex);
        }

        for (int32 i = 0; i < (int32)LightPasses.size(); ++i)
        {
            LightPasses[i].Draw();
        }

        if (!gOptions.UseSubpass)
        {
            check(InRenderPass);
            InRenderPass->EndRenderPass();
        }
    }
}

void jRenderer::PostProcess()
{
    auto AddFullQuadPass = [&](const char* InDebugName, const std::vector<jTexture*> InShaderInputs, const std::shared_ptr<jRenderTarget>& InRenderTargetPtr
        , jName VertexShader, jName PixelShader, bool IsBloom = false, Vector InBloomTintA = Vector::ZeroVector, Vector InBloomTintB = Vector::ZeroVector)
    {
        DEBUG_EVENT(RenderFrameContextPtr, InDebugName);

        static jFullscreenQuadPrimitive* GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

        jRasterizationStateInfo* RasterizationState = nullptr;
        switch (g_rhi->GetSelectedMSAASamples())
        {
        case EMSAASamples::COUNT_1:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_2:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_4:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_8:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
            break;
        default:
            check(0);
            break;
        }
        auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

        const int32 RTWidth = InRenderTargetPtr->Info.Width;
        const int32 RTHeight = InRenderTargetPtr->Info.Height;

        // Create fixed pipeline states
        jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

        const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
        const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

        jRenderPassInfo renderPassInfo;
        jAttachment color = jAttachment(InRenderTargetPtr, EAttachmentLoadStoreOp::DONTCARE_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
        renderPassInfo.Attachments.push_back(color);

        // Setup subpass of LightingPass
        jSubpass subpass;
        subpass.SourceSubpassIndex = 0;
        subpass.DestSubpassIndex = 1;
        subpass.OutputColorAttachments.push_back(0);
        subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
        renderPassInfo.Subpasses.push_back(subpass);

        // Create RenderPass
        jRenderPass* RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { RTWidth, RTHeight });

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
        jShaderBindingInstanceArray ShaderBindingInstanceArray;

        struct jBloomUniformBuffer
        {
            Vector4 BufferSizeAndInvSize;
            Vector4 TintA;
            Vector4 TintB;
            float BloomIntensity;
        };
        jBloomUniformBuffer ubo;
        ubo.BufferSizeAndInvSize.x = (float)RTWidth;
        ubo.BufferSizeAndInvSize.y = (float)RTHeight;
        ubo.BufferSizeAndInvSize.z = 1.0f / (float)RTWidth;
        ubo.BufferSizeAndInvSize.w = 1.0f / (float)RTHeight;
        ubo.TintA = Vector4(InBloomTintA, 0.0);
        ubo.TintB = Vector4(InBloomTintB, 0.0);
        ubo.BloomIntensity = 0.675f;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("BloomUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ubo)));
        if (IsBloom)
        {
            OneFrameUniformBuffer->UpdateBufferData(&ubo, sizeof(ubo));
        }

        std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance;
        {
            if (IsBloom)
            {
                ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
                    , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true);
            }

            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

            for (int32 i = 0; i < (int32)InShaderInputs.size(); ++i)
            {
                ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                    , ResourceInlineAllactor.Alloc<jTextureResource>(InShaderInputs[i], SamplerState));

                g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InShaderInputs[i], EImageLayout::SHADER_READ_ONLY);
            }

            ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
            ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());
        }

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTargetPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);

        jGraphicsPipelineShader Shader;
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(VertexShader);
            shaderInfo.SetShaderFilepath(VertexShader);
            shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
            Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

            shaderInfo.SetName(PixelShader);
            shaderInfo.SetShaderFilepath(PixelShader);
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
        }

        jDrawCommand DrawCommand(RenderFrameContextPtr, GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
            , Shader, &PostProcessPassPipelineStateFixed, GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
        DrawCommand.Test = true;
        DrawCommand.PrepareToDraw(false);

        if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            DrawCommand.Draw();
            RenderPass->EndRenderPass();
        }
    };

    SCOPE_CPU_PROFILE(PostProcess);
    if (1)
    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, PostProcess);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "PostProcess", Vector4(0.0f, 0.8f, 0.8f, 1.0f));

        const uint32 imageIndex = RenderFrameContextPtr->FrameIndex;
        char szDebugEventTemp[1024] = { 0, };

        jSceneRenderTarget* SceneRT = RenderFrameContextPtr->SceneRenderTargetPtr.get();
        jTexture* SourceRT = nullptr;
        jTexture* EyeAdaptationTextureCurrent = nullptr;
        if (gOptions.BloomEyeAdaptation)
        {
            //////////////////////////////////////////////////////////////////////////
            // Todo remove this hardcode
            if (!g_EyeAdaptationARTPtr)
            {
                jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue::Invalid, (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV) };
                Info.ResourceName = TEXT("g_EyeAdaptationARTPtr");
                g_EyeAdaptationARTPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            if (!g_EyeAdaptationBRTPtr)
            {
                jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue::Invalid, (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV) };
                Info.ResourceName = TEXT("g_EyeAdaptationBRTPtr");
                g_EyeAdaptationBRTPtr = jRenderTargetPool::GetRenderTarget(Info);
            }

            static bool FlipEyeAdaptation = false;
            FlipEyeAdaptation = !FlipEyeAdaptation;

            jTexture* EyeAdaptationTextureOld = FlipEyeAdaptation ? g_EyeAdaptationARTPtr->GetTexture() : g_EyeAdaptationBRTPtr->GetTexture();
            EyeAdaptationTextureCurrent = FlipEyeAdaptation ? g_EyeAdaptationBRTPtr->GetTexture() : g_EyeAdaptationARTPtr->GetTexture();

            if (1)
            {
                jCommandBuffer* CommandBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

                g_rhi->TransitionImageLayout(CommandBuffer, EyeAdaptationTextureOld, EImageLayout::SHADER_READ_ONLY);
                //////////////////////////////////////////////////////////////////////////

                g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);

                SourceRT = SceneRT->ColorPtr->GetTexture();

                sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomEyeAdaptationSetup %dx%d", SceneRT->BloomSetup->Info.Width, SceneRT->BloomSetup->Info.Height);
                AddFullQuadPass(szDebugEventTemp, { SourceRT, EyeAdaptationTextureOld }, SceneRT->BloomSetup
                    , jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_and_eyeadaptation_setup_ps.hlsl"));
                SourceRT = SceneRT->BloomSetup->GetTexture();
                g_rhi->TransitionImageLayout(CommandBuffer, SourceRT, EImageLayout::SHADER_READ_ONLY);

                for (int32 i = 0; i < _countof(SceneRT->DownSample); ++i)
                {
                    const auto& RTInfo = SceneRT->DownSample[i]->Info;
                    sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomDownsample %dx%d", RTInfo.Width, RTInfo.Height);
                    AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->DownSample[i]
                        , jNameStatic("Resource/Shaders/hlsl/bloom_down_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_down_ps.hlsl"), true);
                    SourceRT = SceneRT->DownSample[i]->GetTexture();
                    g_rhi->TransitionImageLayout(CommandBuffer, SourceRT, EImageLayout::SHADER_READ_ONLY);
                }
            }

            // Todo make a function for each postprocess steps
            // 여기서 EyeAdaptation 계산하는 Compute shader 추가
            if (1)
            {
                sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "EyeAdaptationCS %dx%d", EyeAdaptationTextureCurrent->Width, EyeAdaptationTextureCurrent->Height);
                DEBUG_EVENT(RenderFrameContextPtr, szDebugEventTemp);
                //////////////////////////////////////////////////////////////////////////
                // Compute Pipeline
                g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SourceRT, EImageLayout::SHADER_READ_ONLY);
                g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), EyeAdaptationTextureCurrent, EImageLayout::UAV);

                std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
                int32 BindingPoint = 0;
                jShaderBindingArray ShaderBindingArray;
                jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

                // Binding 0 : Source Log2Average Image
                if (ensure(SourceRT))
                {
                    ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jTextureResource>(SourceRT, nullptr));
                }

                // Binding 1 : Prev frame EyeAdaptation Image
                if (ensure(EyeAdaptationTextureOld))
                {
                    ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureOld, nullptr));
                }

                // Binding 2 : Current frame EyeAdaptation Image
                if (ensure(EyeAdaptationTextureCurrent))
                {
                    ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureCurrent, nullptr));
                }

                // Binding 3 : CommonComputeUniformBuffer
                struct jEyeAdaptationUniformBuffer
                {
                    Vector2 ViewportMin;
                    Vector2 ViewportMax;
                    float MinLuminanceAverage;
                    float MaxLuminanceAverage;
                    float DeltaFrametime;
                    float AdaptationSpeed;
                    float ExposureCompensation;
                };
                jEyeAdaptationUniformBuffer EyeAdaptationUniformBuffer;
                EyeAdaptationUniformBuffer.ViewportMin = Vector2(0.0f, 0.0f);
                EyeAdaptationUniformBuffer.ViewportMax = Vector2((float)SourceRT->Width, (float)SourceRT->Height);
                EyeAdaptationUniformBuffer.MinLuminanceAverage = 0.03f;
                EyeAdaptationUniformBuffer.MaxLuminanceAverage = 8.0f;
                EyeAdaptationUniformBuffer.DeltaFrametime = 1.0f / 60.0f;
                EyeAdaptationUniformBuffer.AdaptationSpeed = 1.0f;
                EyeAdaptationUniformBuffer.ExposureCompensation = exp2(gOptions.AutoExposureKeyValueScale);

                auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                    jNameStatic("EyeAdaptationUniformBuffer"), jLifeTimeType::OneFrame, sizeof(EyeAdaptationUniformBuffer)));
                OneFrameUniformBuffer->UpdateBufferData(&EyeAdaptationUniformBuffer, sizeof(EyeAdaptationUniformBuffer));
                {
                    ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true);
                }

                CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

                jShaderInfo shaderInfo;
                shaderInfo.SetName(jNameStatic("eyeadaptation"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/eyeadaptation_cs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                static jShader* Shader = g_rhi->CreateShader(shaderInfo);

                jShaderBindingLayoutArray ShaderBindingLayoutArray;
                ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

                jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

                computePipelineStateInfo->Bind(RenderFrameContextPtr);

                //CurrentBindingInstance->BindCompute(RenderFrameContextPtr, (VkPipelineLayout)computePipelineStateInfo->GetPipelineLayoutHandle());

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

                g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), computePipelineStateInfo, ShaderBindingInstanceCombiner, 0);
                g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
            }
            //////////////////////////////////////////////////////////////////////////

            if (1)
            {
                Vector UpscaleBloomTintA[3] = {
                    Vector(0.066f, 0.066f, 0.066f) * 0.675f,
                    Vector(0.1176f, 0.1176f, 0.1176f) * 0.675f,
                    Vector(0.138f, 0.138f, 0.138f) * 0.675f * 0.5f
                };

                Vector UpscaleBloomTintB[3] = {
                    Vector(0.066f, 0.066f, 0.066f) * 0.675f,
                    Vector::OneVector,
                    Vector::OneVector
                };

                jCommandBuffer_DX12* CommandBuffer = (jCommandBuffer_DX12*)RenderFrameContextPtr->GetActiveCommandBuffer();
                for (int32 i = 0; i < _countof(SceneRT->UpSample); ++i)
                {
                    const auto& RTInfo = SceneRT->UpSample[i]->Info;
                    sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomUpsample %dx%d", RTInfo.Width, RTInfo.Height);
                    AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->UpSample[i]
                        , jNameStatic("Resource/Shaders/hlsl/bloom_up_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_up_ps.hlsl"), true, UpscaleBloomTintA[i], UpscaleBloomTintB[i]);
                    SourceRT = SceneRT->UpSample[i]->GetTexture();
                    g_rhi->TransitionImageLayout(CommandBuffer, SourceRT, EImageLayout::SHADER_READ_ONLY);
                }

                g_rhi->TransitionImageLayout(CommandBuffer, EyeAdaptationTextureCurrent, EImageLayout::SHADER_READ_ONLY);
            }
        }
        else
        {
            SourceRT = GBlackTexture;
            EyeAdaptationTextureCurrent = GWhiteTexture;
        }
        sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "Tonemap %dx%d", SceneRT->FinalColorPtr->Info.Width, SceneRT->FinalColorPtr->Info.Height);
        AddFullQuadPass(szDebugEventTemp, { SourceRT, SceneRT->ColorPtr->GetTexture(), EyeAdaptationTextureCurrent }, SceneRT->FinalColorPtr
            , jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/tonemap_ps.hlsl"));
    }
}

void jRenderer::DebugPasses()
{
    if (!gOptions.ShowDebugObject)
        return;

    SCOPE_CPU_PROFILE(DebugPasses);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, DebugPasses);
    DEBUG_EVENT(RenderFrameContextPtr, "DebugPasses");

    // Prepare basepass pipeline
    jRasterizationStateInfo* RasterizationState = nullptr;
    switch (g_rhi->GetSelectedMSAASamples())
    {
    case EMSAASamples::COUNT_1:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_2:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_4:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_8:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
        break;
    default:
        check(0);
        break;
    }
    auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo DebugPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr, EAttachmentLoadStoreOp::LOAD_DONTCARE
        , EAttachmentLoadStoreOp::LOAD_DONTCARE, ClearDepth
        , RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetLayout(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        {
            resolve = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT, true);
        }
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

    //if (UseForwardRenderer || gOptions.UseSubpass)
    {
        jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr, EAttachmentLoadStoreOp::LOAD_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
            , RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr->GetLayout(), EImageLayout::COLOR_ATTACHMENT);
        renderPassInfo.Attachments.push_back(color);
    }

    const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
    renderPassInfo.Attachments.push_back(depth);

    const int32 ResolveAttachemntIndex = (int32)renderPassInfo.Attachments.size();
    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            renderPassInfo.Attachments.push_back(resolve);
    }

    //////////////////////////////////////////////////////////////////////////
    // Setup subpass of BasePass
    {
        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
        subpass.OutputColorAttachments.push_back(0);
        subpass.OutputDepthAttachment = DepthAttachmentIndex;
        if (UseForwardRenderer)
        {
            if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
                subpass.OutputResolveAttachment = ResolveAttachemntIndex;
        }
        renderPassInfo.Subpasses.push_back(subpass);
    }
    jRenderPass* DebugRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

    jGraphicsPipelineShader DebugObjectShader;
    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("default_debug_objectVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/debug_object_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        DebugObjectShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        shaderInfo.SetName(jNameStatic("default_debug_objectPS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/debug_object_fs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
        DebugObjectShader.PixelShader = g_rhi->CreateShader(shaderInfo);
    }

    std::vector<jDrawCommand> DebugDrawCommand;
    auto& DebugObjects = jObject::GetDebugObject();
    DebugDrawCommand.resize(DebugObjects.size());
    for (int32 i = 0; i < (int32)DebugObjects.size(); ++i)
    {
        new (&DebugDrawCommand[i]) jDrawCommand(RenderFrameContextPtr, &View, DebugObjects[i]->RenderObjects[0], DebugRenderPass
            , DebugObjectShader, &DebugPassPipelineStateFixed, DebugObjects[i]->RenderObjects[0]->MaterialPtr.get(), {}, nullptr);
        DebugDrawCommand[i].PrepareToDraw(false);
    }

    if (DebugRenderPass && DebugRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
    {
        for (auto& command : DebugDrawCommand)
        {
            command.Draw();
        }
        DebugRenderPass->EndRenderPass();
    }
}

void jRenderer::Render()
{
    SCOPE_CPU_PROFILE(Render);

    {
        SCOPE_CPU_PROFILE(PoolReset);
        check(RenderFrameContextPtr->GetActiveCommandBuffer());
        ensure(RenderFrameContextPtr->GetActiveCommandBuffer()->Begin());
        if (g_rhi->GetQueryTimePool())
        {
            g_rhi->GetQueryTimePool()->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
        }
        if (g_rhi->GetQueryOcclusionPool())
        {
            g_rhi->GetQueryOcclusionPool()->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
        }

        //ShadowpassOcclusionTest.Init();
        //BasepassOcclusionTest.Init();
    }



#if 1
    jSceneRenderTarget::CubeEnvMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_cubemp.dds")).lock().get();
    jSceneRenderTarget::IrradianceMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_irradiancemap.dds")).lock().get();
    jSceneRenderTarget::FilteredEnvMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_filteredenvmap.dds")).lock().get();
#else
    static jName FilePath = jName("Resource/stpeters_probe.hdr");
    static auto Cubemap = jRHIUtil::ConvertToCubeMap(jNameStatic("F:\\Cubemap.dds"), { 512, 512 }, RenderFrameContextPtr, FilePath);
    static auto IrradianceMap = jRHIUtil::GenerateIrradianceMap(jNameStatic("F:\\IrradianceMap.dds"), { 256, 256 }, RenderFrameContextPtr, Cubemap->GetTexture());
    static auto EnvironmentCubeMap = jRHIUtil::GenerateFilteredEnvironmentMap(jNameStatic("F:\\FilteredEnvMap.dds"), { 256, 256 }, RenderFrameContextPtr, Cubemap->GetTexture());

    jSceneRenderTarget::CubeEnvMap2 = Cubemap->GetTexture();
    jSceneRenderTarget::IrradianceMap2 = IrradianceMap->GetTexture();
    jSceneRenderTarget::FilteredEnvMap2 = EnvironmentCubeMap->GetTexture();
#endif

    static ComPtr<ID3D12StateObject> m_dxrStateObject;
    static ComPtr<ID3D12Resource> m_rayGenShaderTable;
    static ComPtr<ID3D12Resource> m_missShaderTable;
    static ComPtr<ID3D12Resource> m_hitGroupShaderTable;
    if (1)
    {
        static bool once = false;
        if (!once)
        {
            once = true;

            m_raytracingOutput = jBufferUtil_DX12::CreateImage((uint32)SCR_WIDTH, (uint32)SCR_HEIGHT, (uint32)1, (uint32)1, (uint32)1
                , ETextureType::TEXTURE_2D, GetDX12TextureFormat(BackbufferFormat), ETextureCreateFlag::UAV, EImageLayout::UAV);

            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("RTShaer"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RaytracingCubeAndPlane.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING);
            shaderInfo.SetEntryPoint(jNameStatic(""));
            auto raytracingShader = g_rhi->CreateShader(shaderInfo);

            std::array<D3D12_STATE_SUBOBJECT, 20> subobjects;
            uint32 index = 0;

            const wchar_t* c_raygenShaderName = L"MyRaygenShader";
            const wchar_t* c_closestHitShaderName = L"MyClosestHitShader";
            const wchar_t* c_anyHitShaderName = L"MyAnyHitShader";
            const wchar_t* c_missShaderName = L"MyMissShader";
            const wchar_t* c_triHitGroupName = L"TriHitGroup";

            const wchar_t* c_shadowClosestHitShaderName = L"ShadowMyClosestHitShader";
            const wchar_t* c_shadowAnyHitShaderName = L"ShadowMyAnyHitShader";
            const wchar_t* c_shadowMissShaderName = L"ShadowMyMissShader";
            const wchar_t* c_shadowTriHitGroupName = L"ShadowTriHitGroup";

            // 9. CreateRootSignatures
            {
                // global root signature는 DispatchRays 함수 호출로 만들어지는 레이트레이싱 쉐이더의 전체에 공유됨.
                CD3DX12_DESCRIPTOR_RANGE ranges[1];		// 가장 빈번히 사용되는 것을 앞에 둘 수록 최적화에 좋음
                ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);		// 1 output texture

                CD3DX12_ROOT_PARAMETER rootParameters[3];
                rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
                rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
                rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);

                CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(_countof(rootParameters), rootParameters);
                globalRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
                ComPtr<ID3DBlob> blob;
                ComPtr<ID3DBlob> error;
                if (JFAIL(D3D12SerializeRootSignature(&globalRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
                {
                    if (error)
                    {
                        OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
                        error->Release();
                    }
                    return;
                }

                if (JFAIL(g_rhi_dx12->Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
                    , IID_PPV_ARGS(&m_raytracingGlobalRootSignature))))
                {
                    return;
                }
            }

            // 1). DXIL 라이브러리 생성
            D3D12_DXIL_LIBRARY_DESC dxilDesc{};
            std::vector<D3D12_EXPORT_DESC> exportDesc;
            std::vector<std::wstring> exportName;
            {
                D3D12_STATE_SUBOBJECT subobject{};
                {
                    const wchar_t* entryPoint[] = { c_raygenShaderName, c_closestHitShaderName, c_anyHitShaderName, c_missShaderName
                        , c_shadowClosestHitShaderName, c_shadowAnyHitShaderName, c_shadowMissShaderName
                    };
                    subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
                    subobject.pDesc = &dxilDesc;

                    exportDesc.resize(_countof(entryPoint));
                    exportName.resize(_countof(entryPoint));

                    auto CompiledShaderDX12 = (jCompiledShader_DX12*)raytracingShader->CompiledShader;
                    dxilDesc.DXILLibrary.pShaderBytecode = CompiledShaderDX12->ShaderBlob->GetBufferPointer();
                    dxilDesc.DXILLibrary.BytecodeLength = CompiledShaderDX12->ShaderBlob->GetBufferSize();
                    dxilDesc.NumExports = _countof(entryPoint);
                    dxilDesc.pExports = exportDesc.data();

                    for (uint32 i = 0; i < _countof(entryPoint); ++i)
                    {
                        exportName[i] = entryPoint[i];
                        exportDesc[i].Name = exportName[i].c_str();
                        exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
                        exportDesc[i].ExportToRename = nullptr;
                    }
                }
                subobjects[index++] = subobject;
            }

            // 2). Triangle and plane hit group
            // Triangle hit group
            D3D12_HIT_GROUP_DESC hitgroupDesc{};
            {
                hitgroupDesc.AnyHitShaderImport = c_anyHitShaderName;
                hitgroupDesc.ClosestHitShaderImport = c_closestHitShaderName;
                hitgroupDesc.HitGroupExport = c_triHitGroupName;
                hitgroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

                D3D12_STATE_SUBOBJECT subobject{};
                subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
                subobject.pDesc = &hitgroupDesc;
                subobjects[index++] = subobject;
            }

            // Shadow hit group
            D3D12_HIT_GROUP_DESC shadowHitgroupDesc{};
            {
                shadowHitgroupDesc.AnyHitShaderImport = c_shadowAnyHitShaderName;
                shadowHitgroupDesc.ClosestHitShaderImport = c_shadowClosestHitShaderName;
                shadowHitgroupDesc.HitGroupExport = c_shadowTriHitGroupName;
                shadowHitgroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

                D3D12_STATE_SUBOBJECT subobject{};
                subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
                subobject.pDesc = &shadowHitgroupDesc;
                subobjects[index++] = subobject;
            }

            // 3). Shader Config
            D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
            {
                shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);	// float2 barycentrics
                shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float);		// float4 color

                D3D12_STATE_SUBOBJECT subobject{};
                subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
                subobject.pDesc = &shaderConfig;
                subobjects[index++] = subobject;
            }

            // 5). Global root signature
            {
                D3D12_STATE_SUBOBJECT subobject{};
                subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
                subobject.pDesc = m_raytracingGlobalRootSignature.GetAddressOf();
                subobjects[index++] = subobject;
            }

            // 6). Pipeline Config
            D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
            {
                // pipelineConfig.MaxTraceRecursionDepth = D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;
                pipelineConfig.MaxTraceRecursionDepth = 2;

                D3D12_STATE_SUBOBJECT subobject{};
                subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
                subobject.pDesc = &pipelineConfig;

                subobjects[index++] = subobject;
            }

            // Create pipeline state
            D3D12_STATE_OBJECT_DESC stateObjectDesc;
            stateObjectDesc.NumSubobjects = index;
            stateObjectDesc.pSubobjects = subobjects.data();
            stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

            if (JFAIL(g_rhi_dx12->Device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_dxrStateObject))))
                return;

            // 13. ShaderTable
            const uint16 shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
            ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
            if (JFAIL(m_dxrStateObject.As(&stateObjectProperties)))
                return;


            void* rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
            void* misssShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
            void* triHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_triHitGroupName);

            void* shadowMisssShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_shadowMissShaderName);
            void* shadowTriHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_shadowTriHitGroupName);

            // Raygen shader table
            {
                const uint16 numShaderRecords = 1;
                const uint16 shaderRecordSize = shaderIdentifierSize;
                ShaderTable rayGenShaderTable(g_rhi_dx12->Device.Get(), numShaderRecords, shaderRecordSize, TEXT("RayGenShaderTable"));
                rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
                m_rayGenShaderTable = rayGenShaderTable.GetResource();
            }

            // Miss shader table
            {
                const uint16 numShaderRecords = 2;
                const uint16 shaderRecordSize = shaderIdentifierSize;
                ShaderTable missShaderTable(g_rhi_dx12->Device.Get(), numShaderRecords, shaderRecordSize, TEXT("MissShaderTable"));
                missShaderTable.push_back(ShaderRecord(misssShaderIdentifier, shaderIdentifierSize));
                missShaderTable.push_back(ShaderRecord(shadowMisssShaderIdentifier, shaderIdentifierSize));
                m_missShaderTable = missShaderTable.GetResource();
            }

            // Hit shader table
            {
                const uint16 numShaderRecords = 2;
                const uint16 shaderRecordSize = shaderIdentifierSize;
                ShaderTable hitShaderTable(g_rhi_dx12->Device.Get(), numShaderRecords, shaderRecordSize, TEXT("HitShaderTable"));
                hitShaderTable.push_back(ShaderRecord(triHitGroupShaderIdentifier, shaderIdentifierSize));
                hitShaderTable.push_back(ShaderRecord(shadowTriHitGroupShaderIdentifier, shaderIdentifierSize));
                m_hitGroupShaderTable = hitShaderTable.GetResource();
            }
        }

        auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();
        auto CmdBufferDX12 = (jCommandBuffer_DX12*)CmdBuffer;

        if (CmdBufferDX12)
        {
            struct SceneConstantBuffer
            {
                XMMATRIX projectionToWorld;
                XMVECTOR cameraPosition;
                XMVECTOR lightDireciton;
            };

            SceneConstantBuffer m_sceneCB;

            auto GetScreenAspect = []()
            {
                return static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT);
            };

            auto mainCamera = jCamera::GetMainCamera();

            auto v = jCameraUtil::CreateViewMatrix({ 0, 0, -1000 }, { 300, 0, 0 }, { 0, 1, 0 });
            auto p = jCameraUtil::CreatePerspectiveMatrix((float)SCR_WIDTH, (float)SCR_HEIGHT, XMConvertToRadians(45), 1.0f, 1250.0f);

            XMVECTOR m_eye = { mainCamera->Pos.x, mainCamera->Pos.y, mainCamera->Pos.z, 1.0f };
            XMVECTOR m_at = { mainCamera->Target.x, mainCamera->Target.y, mainCamera->Target.z, 1.0f };
            XMVECTOR m_up = { 0.0f, 1.0f, 0.0f };

            m_sceneCB.cameraPosition = m_eye;
            const float fovAngleY = 45.0f;
            const float m_aspectRatio = GetScreenAspect();
            const XMMATRIX view = XMMatrixLookAtLH(m_eye, m_at, m_up);
            const XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovAngleY), m_aspectRatio, 1.0f, 1250.0f);
            const XMMATRIX viewProj = view * proj;
            m_sceneCB.projectionToWorld = XMMatrixTranspose(XMMatrixInverse(nullptr, viewProj));

            jDirectionalLight* DirectionalLight = nullptr;
            for (auto light : jLight::GetLights())
            {
                if (light->Type == ELightType::DIRECTIONAL)
                {
                    DirectionalLight = (jDirectionalLight*)light;
                    break;
                }
            }
            check(DirectionalLight);
            m_sceneCB.lightDireciton = { DirectionalLight->GetLightData().Direction.x, DirectionalLight->GetLightData().Direction.y, DirectionalLight->GetLightData().Direction.z };

            static auto SceneBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(m_sceneCB), 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &m_sceneCB, sizeof(m_sceneCB));
            static auto TempBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(m_sceneCB), 0, EBufferCreateFlag::CPUAccess, D3D12_RESOURCE_STATE_COMMON, &m_sceneCB, sizeof(m_sceneCB));
            TempBuffer->UpdateBuffer(&m_sceneCB, sizeof(m_sceneCB));
            jBufferUtil_DX12::CopyBuffer(CmdBufferDX12->CommandList.Get(), TempBuffer->Buffer.Get(), SceneBuffer->Buffer.Get(), sizeof(m_sceneCB), 0, 0);
            auto cbGpuAddress = SceneBuffer->GetGPUAddress();

            // 현재 디스크립터에 복사해야 함.
            ID3D12DescriptorHeap* ppHeaps[] =
            {
                CmdBufferDX12->OnlineDescriptorHeap->GetHeap(),
                CmdBufferDX12->OnlineSamplerDescriptorHeap->GetHeap()
            };

            const D3D12_GPU_DESCRIPTOR_HANDLE FirstGPUDescriptorHandle
                = CmdBufferDX12->OnlineDescriptorHeap->GetGPUHandle(CmdBufferDX12->OnlineDescriptorHeap->GetNumOfAllocated());
            const D3D12_GPU_DESCRIPTOR_HANDLE FirstGPUSamplerDescriptorHandle
                = CmdBufferDX12->OnlineSamplerDescriptorHeap->GetGPUHandle(CmdBufferDX12->OnlineSamplerDescriptorHeap->GetNumOfAllocated());

            std::vector<jDescriptor_DX12> Descriptors;
            std::vector<jDescriptor_DX12> SamplerDescriptors;

            Descriptors.push_back(m_raytracingOutput->UAV);
            Descriptors.push_back(((jTexture_DX12*)jSceneRenderTarget::IrradianceMap2)->SRV);
            Descriptors.push_back(((jTexture_DX12*)jSceneRenderTarget::FilteredEnvMap2)->SRV);
            for(int32 i=0;i<jObject::GetStaticRenderObject().size();++i)
            {
                jRenderObject* RObj = jObject::GetStaticRenderObject()[i];
                Descriptors.push_back(RObj->VertexAndIndexOffsetBuffer->SRV);

                auto Vtx = (jVertexBuffer_DX12*)RObj->GeometryDataPtr->VertexBuffer;
                auto Idx = (jIndexBuffer_DX12*)RObj->GeometryDataPtr->IndexBuffer;
                Descriptors.push_back(Idx->BufferPtr->SRV);

                RObj->CreateShaderBindingInstance();
                auto RObjUni = (jUniformBufferBlock_DX12*)RObj->TestUniformBuffer.get();
                Descriptors.push_back(RObj->TestUniformBuffer->SRV);

                for (int32 k = 0; k < Vtx->Streams.size(); ++k)
                {
                    if (Vtx->Streams[k].Name == jNameStatic("POSITION"))
                    {
                        Descriptors.push_back(Vtx->Streams[k].BufferPtr->SRV);
                    }
                    else if (Vtx->Streams[k].Name == jNameStatic("NORMAL"))
                    {
                        Descriptors.push_back(Vtx->Streams[k].BufferPtr->SRV);
                    }
                    else if (Vtx->Streams[k].Name == jNameStatic("TANGENT"))
                    {
                        Descriptors.push_back(Vtx->Streams[k].BufferPtr->SRV);
                    }
                    else if (Vtx->Streams[k].Name == jNameStatic("BITANGENT"))
                    {
                        Descriptors.push_back(Vtx->Streams[k].BufferPtr->SRV);
                    }
                    else if (Vtx->Streams[k].Name == jNameStatic("TEXCOORD"))
                    {
                        Descriptors.push_back(Vtx->Streams[k].BufferPtr->SRV);
                        break;
                    }
                }

                if (jTexture_DX12* AlbedoTexDX12 = RObj->MaterialPtr->GetTexture<jTexture_DX12>(jMaterial::EMaterialTextureType::Albedo))
                {
                    check(AlbedoTexDX12->SRV.IsValid());
                    Descriptors.push_back(AlbedoTexDX12->SRV);
                }
                else
                {
                    Descriptors.push_back(((jTexture_DX12*)GBlackTexture)->SRV);
                }

                if (jTexture_DX12* NormalTexDX12 = RObj->MaterialPtr->GetTexture<jTexture_DX12>(jMaterial::EMaterialTextureType::Normal))
                {
                    check(NormalTexDX12->SRV.IsValid());
                    Descriptors.push_back(NormalTexDX12->SRV);
                }
                else
                {
                    Descriptors.push_back(((jTexture_DX12*)GNormalTexture)->SRV);
                }

                if (jTexture_DX12* MetalicTexDX12 = RObj->MaterialPtr->GetTexture<jTexture_DX12>(jMaterial::EMaterialTextureType::Metallic))
                {
                    check(MetalicTexDX12->SRV.IsValid());
                    Descriptors.push_back(MetalicTexDX12->SRV);
                }
                else
                {
                    Descriptors.push_back(((jTexture_DX12*)GBlackTexture)->SRV);
                }
            }
            if (Descriptors.size() > 0)
            {
                check(Descriptors.size() <= 1150);
                jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 1150> DestDescriptor;
                jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 1150> SrcDescriptor;

                for (int32 i = 0; i < Descriptors.size(); ++i)
                {
                    check(Descriptors[i].IsValid());

                    SrcDescriptor.Add(Descriptors[i].CPUHandle);

                    jDescriptor_DX12 Descriptor = CmdBufferDX12->OnlineDescriptorHeap->Alloc();
                    check(Descriptor.IsValid());
                    DestDescriptor.Add(Descriptor.CPUHandle);
                }

                g_rhi_dx12->Device->CopyDescriptors((uint32)DestDescriptor.NumOfData, &DestDescriptor[0], nullptr
                    , (uint32)SrcDescriptor.NumOfData, &SrcDescriptor[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }

 
            const jSamplerStateInfo_DX12* SamplerState = (jSamplerStateInfo_DX12*)TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();
            SamplerDescriptors.push_back(SamplerState->SamplerSRV);

            const jSamplerStateInfo_DX12* PBRSamplerStateInfoDX12 = (jSamplerStateInfo_DX12*)TSamplerStateInfo<ETextureFilter::NEAREST_MIPMAP_LINEAR, ETextureFilter::NEAREST_MIPMAP_LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();
            SamplerDescriptors.push_back(PBRSamplerStateInfoDX12->SamplerSRV);
            if (SamplerDescriptors.size() > 0)
            {
                check(SamplerDescriptors.size() <= 620);
                jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 620> DestDescriptor;
                jResourceContainer<D3D12_CPU_DESCRIPTOR_HANDLE, 620> SrcDescriptor;

                for (int32 i = 0; i < SamplerDescriptors.size(); ++i)
                {
                    check(SamplerDescriptors[i].IsValid());

                    SrcDescriptor.Add(SamplerDescriptors[i].CPUHandle);

                    jDescriptor_DX12 Descriptor = CmdBufferDX12->OnlineSamplerDescriptorHeap->Alloc();
                    check(Descriptor.IsValid());
                    DestDescriptor.Add(Descriptor.CPUHandle);
                }

                g_rhi_dx12->Device->CopyDescriptors((uint32)DestDescriptor.NumOfData, &DestDescriptor[0], nullptr
                    , (uint32)SrcDescriptor.NumOfData, &SrcDescriptor[0], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            }

            CmdBufferDX12->CommandList->SetPipelineState1(m_dxrStateObject.Get());

            CmdBufferDX12->CommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

            D3D12_DISPATCH_RAYS_DESC dispatchDesc{};
            CmdBufferDX12->CommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

            CmdBufferDX12->CommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, FirstGPUDescriptorHandle);
            CmdBufferDX12->CommandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, jGame::TopLevelASBuffer->GetGPUAddress());
            CmdBufferDX12->CommandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);

            // 각 Shader table은 단 한개의 shader record를 가지기 때문에 stride가 그 사이즈와 동일함
            dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
            dispatchDesc.HitGroupTable.SizeInBytes = 64;
            dispatchDesc.HitGroupTable.StrideInBytes = 64;

            dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
            dispatchDesc.MissShaderTable.SizeInBytes = 64;
            dispatchDesc.MissShaderTable.StrideInBytes = 64;

            dispatchDesc.RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
            dispatchDesc.RayGenerationShaderRecord.SizeInBytes = 64;

            dispatchDesc.Width = SCR_WIDTH;
            dispatchDesc.Height = SCR_HEIGHT;
            dispatchDesc.Depth = 1;

            CmdBufferDX12->CommandList->DispatchRays(&dispatchDesc);
        }

        if (1)
        {
            static jFullscreenQuadPrimitive* GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

            //auto BackBuffer = g_rhi_dx12->GetSwapchainImage(g_rhi_dx12->CurrentFrameIndex)->TexturePtr;
            //auto BackBufferRT = std::make_shared<jRenderTarget>(BackBuffer);
            auto RT = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr;

            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RT->TexturePtr.get(), EImageLayout::COLOR_ATTACHMENT);
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), m_raytracingOutput, EImageLayout::SHADER_READ_ONLY);

            jRasterizationStateInfo* RasterizationState = nullptr;
            switch (g_rhi->GetSelectedMSAASamples())
            {
            case EMSAASamples::COUNT_1:
                RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
                break;
            case EMSAASamples::COUNT_2:
                RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
                break;
            case EMSAASamples::COUNT_4:
                RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
                break;
            case EMSAASamples::COUNT_8:
                RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
                break;
            default:
                check(0);
                break;
            }
            auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
            auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

            // Create fixed pipeline states
            jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
                , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

            const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
            const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

            jRenderPassInfo renderPassInfo;
            jAttachment color = jAttachment(RT, EAttachmentLoadStoreOp::LOAD_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);

            jSubpass subpass;
            subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
            subpass.OutputColorAttachments.push_back(0);
            renderPassInfo.Subpasses.push_back(subpass);

            auto RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

            int32 BindingPoint = 0;
            jShaderBindingArray ShaderBindingArray;
            jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
                , ResourceInlineAllactor.Alloc<jTextureResource>(m_raytracingOutput, SamplerState));

            std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
            jShaderBindingInstanceArray ShaderBindingInstanceArray;
            ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());

            jGraphicsPipelineShader Shader;
            {
                jShaderInfo shaderInfo;
                shaderInfo.SetName(jNameStatic("CopyVS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

                shaderInfo.SetName(jNameStatic("CopyPS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
            }

            jDrawCommand DrawCommand(RenderFrameContextPtr, GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
                , Shader, &PostProcessPassPipelineStateFixed, GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
            DrawCommand.Test = true;
            DrawCommand.PrepareToDraw(false);
            if (RenderPass->BeginRenderPass(CmdBufferDX12))
            {
                DrawCommand.Draw();
                RenderPass->EndRenderPass();
            }
        }
    }
    else
    {
        Setup();
        ShadowPass();

        // Queue submit to prepare shadowmap for basepass 
        if (gOptions.QueueSubmitAfterShadowPass)
        {
            SCOPE_CPU_PROFILE(QueueSubmitAfterShadowPass);
            //RenderFrameContextPtr->GetActiveCommandBuffer()->End();
            RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::ShadowPass);
            RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
        }

        BasePass();

        // Queue submit to prepare scenecolor RT for postprocess
        if (gOptions.QueueSubmitAfterBasePass)
        {
            SCOPE_CPU_PROFILE(QueueSubmitAfterBasePass);
            //RenderFrameContextPtr->GetActiveCommandBuffer()->End();
            RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::BasePass);
            RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
        }
    }

    PostProcess();
    DebugPasses();

    check(g_ImGUI);
    g_ImGUI->NewFrame([]()
        {
            Vector4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

            char szTitle[128] = { 0, };
            sprintf_s(szTitle, sizeof(szTitle), "RHI : %s", g_rhi->GetRHIName().ToStr());

            ImGui::SetNextWindowPos(ImVec2(27.0f, 27.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350.0f, 682.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin(szTitle);

#if USE_VARIABLE_SHADING_RATE_TIER2
            ImGui::Checkbox("UseVRS", &gOptions.UseVRS);
#endif
            //ImGui::Checkbox("ShowVRSArea", &gOptions.ShowVRSArea);
            //ImGui::Checkbox("ShowGrid", &gOptions.ShowGrid);
            //ImGui::Checkbox("UseWaveIntrinsics", &gOptions.UseWaveIntrinsics);
            {
                if (IsUseVulkan())
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
                    ImGui::EndDisabled();
                    ImGui::Checkbox("UseSubpass", &gOptions.UseSubpass);
                    ImGui::Checkbox("UseMemoryless", &gOptions.UseMemoryless);
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
                    ImGui::Checkbox("[VulkanOnly]UseSubpass", &gOptions.UseSubpass);
                    ImGui::Checkbox("[VulkanOnly]UseMemoryless", &gOptions.UseMemoryless);
                    ImGui::EndDisabled();
                }
            }
            {
                ImGui::Checkbox("ShowDebugObject", &gOptions.ShowDebugObject);
                ImGui::Checkbox("BloomEyeAdaptation", &gOptions.BloomEyeAdaptation);

                ImGui::Checkbox("QueueSubmitAfterShadowPass", &gOptions.QueueSubmitAfterShadowPass);
                ImGui::Checkbox("QueueSubmitAfterBasePass", &gOptions.QueueSubmitAfterBasePass);
                ImGui::SliderFloat("AutoExposureKeyValueScale", &gOptions.AutoExposureKeyValueScale, -12.0f, 12.0f);
            }
            ImGui::Separator();
            //ImGui::Text("PBR properties");
            //ImGui::SliderFloat("Metallic", &gOptions.Metallic, 0.0f, 1.0f);
            //ImGui::SliderFloat("Roughness", &gOptions.Roughness, 0.0f, 1.0f);
            //ImGui::Separator();

            constexpr float IndentSpace = 10.0f;
            const std::thread::id CurrentThreadId = std::this_thread::get_id();
            const ImVec4 OtherThreadColor = { 0.2f, 0.6f, 0.2f, 1.0f };
            {
                ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            }
            ImGui::Separator();
            {
                ImGui::Text("[CPU]");
                const auto& CPUAvgProfileMap = jPerformanceProfile::GetInstance().GetCPUAvgProfileMap();
                double TotalPassesMS = 0.0;
                int32 MostLeastIndent = INT_MAX;
                for (auto& pair : CPUAvgProfileMap)
                {
                    const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
                    const float Indent = IndentSpace * (float)AvgProfile.Indent;
                    if (Indent > 0)
                        ImGui::Indent(Indent);

                    if (CurrentThreadId == AvgProfile.ThreadId)
                        ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
                    else
                        ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);

                    if (Indent > 0)
                        ImGui::Unindent(Indent);

                    // 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
                    // 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
                    if (CurrentThreadId == AvgProfile.ThreadId)
                    {
                        if (MostLeastIndent > AvgProfile.Indent)
                        {
                            MostLeastIndent = AvgProfile.Indent;
                            TotalPassesMS = AvgProfile.AvgElapsedMS;
                        }
                        else if (MostLeastIndent == AvgProfile.Indent)
                        {
                            TotalPassesMS += AvgProfile.AvgElapsedMS;
                        }
                    }
                }
                ImGui::Text("[CPU]Total Passes : %lf ms", TotalPassesMS);
            }
            ImGui::Separator();
            {
                ImGui::Text("[GPU]");
                const auto& GPUAvgProfileMap = jPerformanceProfile::GetInstance().GetGPUAvgProfileMap();
                double TotalPassesMS = 0.0;
                int32 MostLeastIndent = INT_MAX;
                for (auto& pair : GPUAvgProfileMap)
                {
                    const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
                    const float Indent = IndentSpace * (float)AvgProfile.Indent;
                    if (Indent > 0)
                        ImGui::Indent(Indent);

                    if (CurrentThreadId == AvgProfile.ThreadId)
                        ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
                    else
                        ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);

                    if (Indent > 0)
                        ImGui::Unindent(Indent);

                    // 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
                    // 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
                    if (CurrentThreadId == AvgProfile.ThreadId)
                    {
                        if (MostLeastIndent > AvgProfile.Indent)
                        {
                            MostLeastIndent = AvgProfile.Indent;
                            TotalPassesMS = AvgProfile.AvgElapsedMS;
                        }
                        else if (MostLeastIndent == AvgProfile.Indent)
                        {
                            TotalPassesMS += AvgProfile.AvgElapsedMS;
                        }
                    }
                }
                ImGui::Text("[GPU]Total Passes : %lf ms", TotalPassesMS);
            }
            ImGui::Separator();
            //for (auto& pair : CounterMap)
            //{
            //    ImGui::Text("%s : %lld", pair.first.ToStr(), pair.second);
            //}
            ImGui::Separator();
            ImGui::Text("CameraPos : %.2f, %.2f, %.2f", gOptions.CameraPos.x, gOptions.CameraPos.y, gOptions.CameraPos.z);
            ImGui::End();

#if ENABLE_PBR
            ImGui::SetNextWindowPos(ImVec2(400.0f, 27.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(200.0f, 80.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Sun", 0, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderFloat("DirX", &gOptions.SunDir.x, -1.0f, 1.0f);
            ImGui::SliderFloat("DirY", &gOptions.SunDir.y, -1.0f, 1.0f);
            ImGui::SliderFloat("DirZ", &gOptions.SunDir.z, -1.0f, 1.0f);
            ImGui::SliderFloat("AnisoG", &gOptions.AnisoG, 0.0f, 1.0f);
            ImGui::End();

            //ImGui::SetWindowFocus(szTitle);
#endif
        });
    g_ImGUI->Draw(RenderFrameContextPtr);
}
