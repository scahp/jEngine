#include "pch.h"
#include "jRenderer.h"
#include "jOptions.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRaytracingScene.h"
#include "RHI/jRHI.h"
#include "RHI/jRHIUtil.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Material/jMaterial.h"
#include "FileLoader/jImageFileLoader.h"
#include "Shader/jShaderParameterSet.h"
#include <unordered_map>

namespace
{
    BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jHWRTDISceneConstantBuffer)
        SHADER_UNIFORM_BUFFER_MEMBER(Matrix, ProjectionToWorld)
        SHADER_UNIFORM_BUFFER_MEMBER(Vector, CameraPosition)
        SHADER_UNIFORM_BUFFER_MEMBER(float, NormalBias)
        SHADER_UNIFORM_BUFFER_MEMBER(uint32, NumLights)
        SHADER_UNIFORM_BUFFER_MEMBER(uint32, DebugViewMode)
        SHADER_UNIFORM_BUFFER_MEMBER(uint32, ForceMipLevel0)
        SHADER_UNIFORM_BUFFER_MEMBER(uint32, RenderWidth)
        SHADER_UNIFORM_BUFFER_MEMBER(float, DebugLineWidth)
        SHADER_UNIFORM_BUFFER_MEMBER(float, DebugUVScale)
        SHADER_UNIFORM_BUFFER_MEMBER(float, DebugPrimitiveIDScale)
        SHADER_UNIFORM_BUFFER_MEMBER(float, ShadowRayStartOffset)
        SHADER_UNIFORM_BUFFER_MEMBER(uint32, RenderHeight)
        SHADER_UNIFORM_BUFFER_MEMBER(float, Padding0)
        SHADER_UNIFORM_BUFFER_MEMBER(float, Padding1)
        SHADER_UNIFORM_BUFFER_MEMBER(float, Padding2)
    END_SHADER_UNIFORM_BUFFER_STRUCT()

    BEGIN_SHADER_STRUCT(MaterialInstanceUniform)
        SHADER_STRUCT_MEMBER(uint32, MaterialFlags)
        SHADER_STRUCT_MEMBER(uint32, AlbedoSamplerIndex)
        SHADER_STRUCT_MEMBER(uint32, NormalSamplerIndex)
        SHADER_STRUCT_MEMBER(uint32, RMSamplerIndex)
        SHADER_STRUCT_MEMBER(float, AlphaCutoff)
        SHADER_STRUCT_MEMBER(float, Padding0)
        SHADER_STRUCT_MEMBER(float, Padding1)
        SHADER_STRUCT_MEMBER(float, Padding2)
    END_SHADER_STRUCT()

    struct jHWRTDIBindlessUInt2
    {
    };

    template <>
    struct TShaderParameterHLSLTypeInfo<jHWRTDIBindlessUInt2>
    {
        static constexpr const char* GetTypeName() { return "uint2"; }
        static void AppendTypeDeclaration(std::string&) {}
    };

    BEGIN_SHADER_STRUCT(HWRTDILightData)
        SHADER_STRUCT_MEMBER(Vector4, ColorAndType)
        SHADER_STRUCT_MEMBER(Vector4, PositionAndMaxDistance)
        SHADER_STRUCT_MEMBER(Vector4, DirectionAndPenumbra)
        SHADER_STRUCT_MEMBER(Vector4, UmbraAndPadding)
    END_SHADER_STRUCT()

    BEGIN_SHADER_PARAMETER_SET(jHWRTDIGlobalParameters)
        SHADER_ACCELERATION_STRUCTURE(Scene)
        SHADER_RW_TEXTURE2D(RenderTarget)
        SHADER_UNIFORM_BUFFER(jHWRTDISceneConstantBuffer, g_sceneCB)
        SHADER_SAMPLER(DefaultSamplerState)
        SHADER_TEXTURECUBE_SRV(EnvTexture)
        SHADER_STRUCTURED_BUFFER(HWRTDILightData, LightBuffer)
    END_SHADER_PARAMETER_SET()

    BEGIN_SHADER_BINDLESS_SET(jHWRTDIBindlessParameters)
        // Bindless tables are assigned consecutive spaces starting at the binder's current space.
        SHADER_BINDLESS_STRUCTURED_BUFFER(jHWRTDIBindlessUInt2, VertexIndexOffsetArray)
        SHADER_BINDLESS_BUFFER(uint32, IndexBindlessArray)
        SHADER_BINDLESS_STRUCTURED_BUFFER(RenderObjectUniformBuffer, RenderObjParamArray)
        SHADER_BINDLESS_BYTEADDRESS_BUFFER(VerticesBindlessArray)
        SHADER_BINDLESS_UNIFORM_BUFFER(MaterialInstanceUniform, MaterialInstanceArray)
        SHADER_BINDLESS_TEXTURE2D(AlbedoTextureArray)
        SHADER_BINDLESS_TEXTURE2D(NormalTextureArray)
        SHADER_BINDLESS_TEXTURE2D(RMTextureArray)
        SHADER_BINDLESS_SAMPLER(AlbedoSamplerArray)
        SHADER_BINDLESS_SAMPLER(NormalSamplerArray)
        SHADER_BINDLESS_SAMPLER(RMSamplerArray)
    END_SHADER_BINDLESS_SET()

    struct jShaderHWRTDIPrimaryMissShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIPrimaryMissShader, Permutation)
    };

    struct jShaderHWRTDIRaygenShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIRaygenShader, Permutation)
    };

    struct jShaderHWRTDIPrimaryClosestHitShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIPrimaryClosestHitShader, Permutation)
    };

    struct jShaderHWRTDIPrimaryAnyHitShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIPrimaryAnyHitShader, Permutation)
    };

    struct jShaderHWRTDIShadowMissShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIShadowMissShader, Permutation)
    };

    struct jShaderHWRTDIShadowClosestHitShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIShadowClosestHitShader, Permutation)
    };

    struct jShaderHWRTDIShadowAnyHitShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIShadowAnyHitShader, Permutation)
    };

    struct jShaderHWRTDIInlineRayQueryComputeShader : public jShader
    {
        DECLARE_SHADER_PARAMETER_SETS(jHWRTDIGlobalParameters)

        DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jHWRTDIBindlessParameters>();
        }

        DECLARE_SHADER_WITH_PERMUTATION(jShaderHWRTDIInlineRayQueryComputeShader, Permutation)
    };

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIPrimaryMissShader
        , "HWRTDI_Miss"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "PrimaryMissShader"
        , EShaderAccessStageFlag::RAYTRACING_MISS)

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIRaygenShader
        , "HWRTDI_Raygen"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "RaygenShader"
        , EShaderAccessStageFlag::RAYTRACING_RAYGEN)

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIPrimaryClosestHitShader
        , "HWRTDI_ClosestHit"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "PrimaryClosestHitShader"
        , EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT)

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIPrimaryAnyHitShader
        , "HWRTDI_AnyHit"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "PrimaryAnyHitShader"
        , EShaderAccessStageFlag::RAYTRACING_ANYHIT)

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIShadowMissShader
        , "HWRTDI_ShadowMiss"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "ShadowMissShader"
        , EShaderAccessStageFlag::RAYTRACING_MISS)

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIShadowClosestHitShader
        , "HWRTDI_ShadowClosestHit"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "ShadowClosestHitShader"
        , EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT)

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIShadowAnyHitShader
        , "HWRTDI_ShadowAnyHit"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "ShadowAnyHitShader"
        , EShaderAccessStageFlag::RAYTRACING_ANYHIT)

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHWRTDIInlineRayQueryComputeShader
        , "HWRTDI_InlineRayQueryCS"
        , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
        , ""
        , "InlineRayQueryCS"
        , EShaderAccessStageFlag::COMPUTE)

    enum : uint32
    {
        HWRTDI_MaterialFlag_HasAlbedoTexture = 1u << 0,
        HWRTDI_MaterialFlag_HasNormalTexture = 1u << 1,
        HWRTDI_MaterialFlag_HasRMTexture = 1u << 2,
        HWRTDI_MaterialFlag_UseSRGBAlbedoTexture = 1u << 3,
        HWRTDI_MaterialFlag_IsSkyMaterial = 1u << 4,
        HWRTDI_MaterialFlag_UseAlphaCutout = 1u << 5,
        HWRTDI_MaterialFlag_NonOpaqueGeometry = 1u << 6
    };

    enum : int32
    {
        HWRTDI_Mode_DispatchRays = 0,
        HWRTDI_Mode_InlineRayQuery = 1,
        HWRTDI_Mode_Count = 2
    };

    FORCEINLINE int32 ResolveHWRTDirectLightingMode(int32 InRequestedMode)
    {
        int32 ResolvedMode = Clamp(InRequestedMode, 0, HWRTDI_Mode_Count - 1);
        if (ResolvedMode == HWRTDI_Mode_InlineRayQuery && !GSupportInlineRaytracing)
        {
            ResolvedMode = HWRTDI_Mode_DispatchRays;
        }
        return ResolvedMode;
    }

    FORCEINLINE EShaderAccessStageFlag GetHWRTDIBindingShaderStageFlag(bool InUseInlineRayQuery)
    {
        if (InUseInlineRayQuery)
        {
            return EShaderAccessStageFlag::ALL_RAYTRACING | EShaderAccessStageFlag::COMPUTE;
        }
        return EShaderAccessStageFlag::ALL_RAYTRACING;
    }

    void CompositeHWRTDIResult(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, jTexture* InHWRTDIOutput)
    {
        check(InRenderFrameContext);
        check(InHWRTDIOutput);

        jRHIUtil::DrawQuad(InRenderFrameContext, InRenderFrameContext->SceneRenderTargetPtr->ColorPtr, { 0, 0, SCR_WIDTH, SCR_HEIGHT }
            , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllocator)
            {
                g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InHWRTDIOutput, EResourceLayout::SHADER_READ_ONLY);

                const jSamplerStateInfo* CopySamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                    , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                    , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                jRHIUtil::BuildSingleTextureFragmentBindings(InHWRTDIOutput, CopySamplerState, InOutShaderBindingArray, InOutResourceInlineAllocator);
            }
            , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
            {
                jShaderInfo ShaderInfo;
                ShaderInfo.SetName(jNameStatic("HWRTDI_CopyPS"));
                ShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
                ShaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                jRHIUtil::AppendSingleTextureFragmentShaderInfo(ShaderInfo);
                return g_rhi->CreateShader(ShaderInfo);
            });
    }

    void BuildHWRTDIRaytracingShaders(std::vector<jRaytracingPipelineShader>& OutRaytracingShaders)
    {
        OutRaytracingShaders.clear();
        OutRaytracingShaders.reserve(2);

        {
            jRaytracingPipelineShader NewShader;
            jShaderHWRTDIPrimaryMissShader::ShaderPermutation MissPermutation;
            MissPermutation.SetIndex<jShaderHWRTDIPrimaryMissShader::USE_SURFEL_GI>(0);
            MissPermutation.SetIndex<jShaderHWRTDIPrimaryMissShader::USE_BINDLESS_RESOURCE>(1);
            NewShader.MissShader = jShaderHWRTDIPrimaryMissShader::CreateShader(MissPermutation);
            NewShader.MissEntryPoint = TEXT("PrimaryMissShader");

            jShaderHWRTDIRaygenShader::ShaderPermutation RaygenPermutation;
            RaygenPermutation.SetIndex<jShaderHWRTDIRaygenShader::USE_SURFEL_GI>(0);
            RaygenPermutation.SetIndex<jShaderHWRTDIRaygenShader::USE_BINDLESS_RESOURCE>(1);
            NewShader.RaygenShader = jShaderHWRTDIRaygenShader::CreateShader(RaygenPermutation);
            NewShader.RaygenEntryPoint = TEXT("RaygenShader");

            jShaderHWRTDIPrimaryClosestHitShader::ShaderPermutation ClosestHitPermutation;
            ClosestHitPermutation.SetIndex<jShaderHWRTDIPrimaryClosestHitShader::USE_SURFEL_GI>(0);
            ClosestHitPermutation.SetIndex<jShaderHWRTDIPrimaryClosestHitShader::USE_BINDLESS_RESOURCE>(1);
            NewShader.ClosestHitShader = jShaderHWRTDIPrimaryClosestHitShader::CreateShader(ClosestHitPermutation);
            NewShader.ClosestHitEntryPoint = TEXT("PrimaryClosestHitShader");

            jShaderHWRTDIPrimaryAnyHitShader::ShaderPermutation AnyHitPermutation;
            AnyHitPermutation.SetIndex<jShaderHWRTDIPrimaryAnyHitShader::USE_SURFEL_GI>(0);
            AnyHitPermutation.SetIndex<jShaderHWRTDIPrimaryAnyHitShader::USE_BINDLESS_RESOURCE>(1);
            NewShader.AnyHitShader = jShaderHWRTDIPrimaryAnyHitShader::CreateShader(AnyHitPermutation);
            NewShader.AnyHitEntryPoint = TEXT("PrimaryAnyHitShader");

            NewShader.HitGroupName = TEXT("DefaultHit");
            OutRaytracingShaders.push_back(NewShader);
        }

        {
            jRaytracingPipelineShader NewShader;
            jShaderHWRTDIShadowMissShader::ShaderPermutation ShadowMissPermutation;
            ShadowMissPermutation.SetIndex<jShaderHWRTDIShadowMissShader::USE_SURFEL_GI>(0);
            ShadowMissPermutation.SetIndex<jShaderHWRTDIShadowMissShader::USE_BINDLESS_RESOURCE>(1);
            NewShader.MissShader = jShaderHWRTDIShadowMissShader::CreateShader(ShadowMissPermutation);
            NewShader.MissEntryPoint = TEXT("ShadowMissShader");

            jShaderHWRTDIShadowClosestHitShader::ShaderPermutation ShadowClosestHitPermutation;
            ShadowClosestHitPermutation.SetIndex<jShaderHWRTDIShadowClosestHitShader::USE_SURFEL_GI>(0);
            ShadowClosestHitPermutation.SetIndex<jShaderHWRTDIShadowClosestHitShader::USE_BINDLESS_RESOURCE>(1);
            NewShader.ClosestHitShader = jShaderHWRTDIShadowClosestHitShader::CreateShader(ShadowClosestHitPermutation);
            NewShader.ClosestHitEntryPoint = TEXT("ShadowClosestHitShader");

            jShaderHWRTDIShadowAnyHitShader::ShaderPermutation ShadowAnyHitPermutation;
            ShadowAnyHitPermutation.SetIndex<jShaderHWRTDIShadowAnyHitShader::USE_SURFEL_GI>(0);
            ShadowAnyHitPermutation.SetIndex<jShaderHWRTDIShadowAnyHitShader::USE_BINDLESS_RESOURCE>(1);
            NewShader.AnyHitShader = jShaderHWRTDIShadowAnyHitShader::CreateShader(ShadowAnyHitPermutation);
            NewShader.AnyHitEntryPoint = TEXT("ShadowAnyHitShader");
            NewShader.HitGroupName = TEXT("ShadowHit");

            OutRaytracingShaders.push_back(NewShader);
        }
    }

    bool RunHWRTDirectLightingPass(jRenderer* InRenderer, bool InUseInlineRayQuery)
    {
        check(InRenderer);
        check(InRenderer->RenderFrameContextPtr);

        auto* RaytracingScene = InRenderer->RenderFrameContextPtr->RaytracingScene;
        if (!RaytracingScene || !RaytracingScene->TLASBufferPtr || RaytracingScene->InstanceList.empty())
            return false;

        if (!RaytracingScene->RaytracingOutputPtr
            || RaytracingScene->RaytracingOutputPtr->Width != (int32)SCR_WIDTH
            || RaytracingScene->RaytracingOutputPtr->Height != (int32)SCR_HEIGHT
            || RaytracingScene->RaytracingOutputPtr->Format != ETextureFormat::RGBA16F)
        {
            RaytracingScene->RaytracingOutputPtr = g_rhi->Create2DTexture((uint32)SCR_WIDTH, (uint32)SCR_HEIGHT, (uint32)1, (uint32)1
                , ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
        }

        jTexture* HWRTDIOutput = RaytracingScene->RaytracingOutputPtr.get();
        if (!HWRTDIOutput)
            return false;

        const jName HWRTDirectLightingProfileName = InUseInlineRayQuery
            ? jNameStatic("HWRTDirectLightingInlineRayQuery")
            : jNameStatic("HWRTDirectLightingDispatchRays");
        const char* HWRTDirectLightingEventName = InUseInlineRayQuery
            ? "HWRTDirectLightingInlineRayQuery"
            : "HWRTDirectLightingDispatchRays";
        DEBUG_EVENT_WITH_COLOR(InRenderer->RenderFrameContextPtr, HWRTDirectLightingEventName, Vector4(0.8f, 0.2f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE_NAME(HWRTDirectLightingProfileName);
        SCOPE_GPU_PROFILE_NAME(InRenderer->RenderFrameContextPtr, HWRTDirectLightingProfileName);

        auto CmdBuffer = InRenderer->RenderFrameContextPtr->GetActiveCommandBuffer();
        check(CmdBuffer);

        const EShaderAccessStageFlag BindingShaderStageFlag = GetHWRTDIBindingShaderStageFlag(InUseInlineRayQuery);

        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

        jHWRTDISceneConstantBuffer SceneCB;
        SceneCB.ProjectionToWorld = jCamera::GetMainCamera()->GetInverseViewProjectionMatrix();
        SceneCB.CameraPosition = jCamera::GetMainCamera()->Pos;
        SceneCB.DebugViewMode = (gOptions.HWRTDebugViewMode >= 0) ? (uint32)gOptions.HWRTDebugViewMode : 0u;
        SceneCB.DebugLineWidth = (gOptions.HWRTDebugLineWidth > 0.0005f) ? gOptions.HWRTDebugLineWidth : 0.0005f;
        SceneCB.DebugUVScale = (gOptions.HWRTDebugUVScale > 1.0f) ? gOptions.HWRTDebugUVScale : 1.0f;
        SceneCB.DebugPrimitiveIDScale = (gOptions.HWRTDebugPrimitiveIDScale > 0.1f) ? gOptions.HWRTDebugPrimitiveIDScale : 0.1f;
        SceneCB.ForceMipLevel0 = gOptions.HWRTForceMipLevel0 ? 1u : 0u;
        SceneCB.NormalBias = (gOptions.HWRTNormalBias > 0.0f) ? gOptions.HWRTNormalBias : 0.0f;
        SceneCB.ShadowRayStartOffset = (gOptions.HWRTShadowRayStartOffset > 0.0f) ? gOptions.HWRTShadowRayStartOffset : 0.0f;
        SceneCB.RenderWidth = (uint32)SCR_WIDTH;
        SceneCB.RenderHeight = (uint32)SCR_HEIGHT;

        std::vector<const jBuffer*> VertexAndIndexOffsetBuffers;
        std::vector<const jBuffer*> IndexBuffers;
        std::vector<const jBuffer*> RenderObjectBuffers;
        std::vector<const jBuffer*> VertexBuffers;
        std::vector<const IUniformBufferBlock*> MaterialInstanceBuffers;
        std::vector<jTextureResourceBindless::jTextureBindData> AlbedoTextures;
        std::vector<jTextureResourceBindless::jTextureBindData> NormalTextures;
        std::vector<jTextureResourceBindless::jTextureBindData> RMTextures;
        std::vector<const jSamplerStateInfo*> AlbedoSamplerStates;
        std::vector<const jSamplerStateInfo*> NormalSamplerStates;
        std::vector<const jSamplerStateInfo*> RMSamplerStates;
        std::vector<jHWRTDIPackedLight> PackedLights;
        std::vector<std::shared_ptr<IUniformBufferBlock>> RefCountMaintainer;
        std::shared_ptr<jBuffer> PackedLightBuffer;

        const auto CreateOneFrameUniformBuffer = [&](jName Name, const void* Data, uint32 Size)
        {
            auto UniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(Name, jLifeTimeType::OneFrame, Size));
            UniformBuffer->UpdateBufferData(Data, Size);
            RefCountMaintainer.push_back(UniformBuffer);
            return UniformBuffer;
        };

        VertexAndIndexOffsetBuffers.reserve(RaytracingScene->InstanceList.size());
        IndexBuffers.reserve(RaytracingScene->InstanceList.size());
        RenderObjectBuffers.reserve(RaytracingScene->InstanceList.size());
        VertexBuffers.reserve(RaytracingScene->InstanceList.size());
        MaterialInstanceBuffers.reserve(RaytracingScene->InstanceList.size());
        AlbedoTextures.reserve(RaytracingScene->InstanceList.size());
        NormalTextures.reserve(RaytracingScene->InstanceList.size());
        RMTextures.reserve(RaytracingScene->InstanceList.size());
        AlbedoSamplerStates.reserve(RaytracingScene->InstanceList.size());
        NormalSamplerStates.reserve(RaytracingScene->InstanceList.size());
        RMSamplerStates.reserve(RaytracingScene->InstanceList.size());
        PackedLights.reserve(jLight::GetLights().size());

        std::unordered_map<const jSamplerStateInfo*, uint32> AlbedoSamplerIndexMap;
        std::unordered_map<const jSamplerStateInfo*, uint32> NormalSamplerIndexMap;
        std::unordered_map<const jSamplerStateInfo*, uint32> RMSamplerIndexMap;
        AlbedoSamplerIndexMap.reserve(RaytracingScene->InstanceList.size());
        NormalSamplerIndexMap.reserve(RaytracingScene->InstanceList.size());
        RMSamplerIndexMap.reserve(RaytracingScene->InstanceList.size());

        const auto GetOrAddSamplerIndex = [](std::vector<const jSamplerStateInfo*>& InSamplerStates
            , std::unordered_map<const jSamplerStateInfo*, uint32>& InSamplerIndexMap
            , const jSamplerStateInfo* InSamplerState) -> uint32
        {
            check(InSamplerState);

            auto It = InSamplerIndexMap.find(InSamplerState);
            if (It != InSamplerIndexMap.end())
                return It->second;

            const uint32 NewIndex = (uint32)InSamplerStates.size();
            InSamplerStates.push_back(InSamplerState);
            InSamplerIndexMap.emplace(InSamplerState, NewIndex);
            return NewIndex;
        };

        for (jRenderObject* RenderObject : RaytracingScene->InstanceList)
        {
            if (!RenderObject || !RenderObject->GeometryDataPtr || !RenderObject->IsSupportRaytracing())
                return false;

            RenderObject->CreateShaderBindingInstance();

            VertexAndIndexOffsetBuffers.push_back(RenderObject->VertexAndIndexOffsetBuffer.get());
            IndexBuffers.push_back(RenderObject->GeometryDataPtr->IndexBufferPtr->GetBuffer());
            RenderObjectBuffers.push_back(RenderObject->TestUniformBuffer.get());
            VertexBuffers.push_back(RenderObject->GeometryDataPtr->VertexBufferPtr->GetBuffer(0));

            const jMaterial* Material = RenderObject->MaterialPtr ? RenderObject->MaterialPtr.get() : GDefaultMaterial.get();
            check(Material);

            MaterialInstanceUniform MaterialUniform;
            if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Albedo].Texture)
                MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_HasAlbedoTexture;
            if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Normal].Texture)
                MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_HasNormalTexture;
            if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Metallic].Texture)
                MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_HasRMTexture;
            if (Material->IsUseSRGBAlbedoTexture())
                MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_UseSRGBAlbedoTexture;
            const bool IsSkyMaterial = Material->IsUseSphericalMap();
            if (IsSkyMaterial)
                MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_IsSkyMaterial;
            const bool UseAlphaCutout = !IsSkyMaterial
                && Material->HasAlbedoTexture()
                && Material->IsRaytracingAlphaTestEnabled();
            if (UseAlphaCutout)
            {
                MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_UseAlphaCutout;
                MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_NonOpaqueGeometry;
            }
            MaterialUniform.AlphaCutoff = Clamp(Material->RaytracingAlphaCutoff, 0.0f, 1.0f);

            const jSamplerStateInfo* AlbedoSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Albedo);
            const jSamplerStateInfo* NormalSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Normal);
            const jSamplerStateInfo* RMSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Metallic);
            MaterialUniform.AlbedoSamplerIndex = GetOrAddSamplerIndex(AlbedoSamplerStates, AlbedoSamplerIndexMap, AlbedoSamplerState);
            MaterialUniform.NormalSamplerIndex = GetOrAddSamplerIndex(NormalSamplerStates, NormalSamplerIndexMap, NormalSamplerState);
            MaterialUniform.RMSamplerIndex = GetOrAddSamplerIndex(RMSamplerStates, RMSamplerIndexMap, RMSamplerState);

            auto MaterialUniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_MaterialInstance"), &MaterialUniform, sizeof(MaterialUniform));
            MaterialInstanceBuffers.push_back(MaterialUniformBuffer.get());

            AlbedoTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Albedo), nullptr, 0 });
            NormalTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Normal), nullptr, 0 });
            RMTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Metallic), nullptr, 0 });
        }

        for (jLight* Light : jLight::GetLights())
        {
            if (!Light)
                continue;

            switch (Light->Type)
            {
            case ELightType::DIRECTIONAL:
            {
                const auto* DirectionalLight = static_cast<jDirectionalLight*>(Light);
                const jDirectionalLightUniformBufferData& LightData = DirectionalLight->GetLightData();
                jHWRTDIPackedLight PackedLight;
                PackedLight.ColorAndType = Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, (float)static_cast<uint32>(ELightType::DIRECTIONAL));
                PackedLight.DirectionAndPenumbra = Vector4(LightData.Direction.x, LightData.Direction.y, LightData.Direction.z, 0.0f);
                PackedLights.push_back(PackedLight);
                break;
            }
            case ELightType::POINT:
            {
                const auto* PointLight = static_cast<jPointLight*>(Light);
                const jPointLightUniformBufferData& LightData = PointLight->GetLightData();
                jHWRTDIPackedLight PackedLight;
                PackedLight.ColorAndType = Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, (float)static_cast<uint32>(ELightType::POINT));
                PackedLight.PositionAndMaxDistance = Vector4(LightData.Position.x, LightData.Position.y, LightData.Position.z, LightData.MaxDistance);
                PackedLights.push_back(PackedLight);
                break;
            }
            case ELightType::SPOT:
            {
                const auto* SpotLight = static_cast<jSpotLight*>(Light);
                const jSpotLightUniformBufferData& LightData = SpotLight->GetLightData();
                jHWRTDIPackedLight PackedLight;
                PackedLight.ColorAndType = Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, (float)static_cast<uint32>(ELightType::SPOT));
                PackedLight.PositionAndMaxDistance = Vector4(LightData.Position.x, LightData.Position.y, LightData.Position.z, LightData.MaxDistance);
                PackedLight.DirectionAndPenumbra = Vector4(LightData.Direction.x, LightData.Direction.y, LightData.Direction.z, LightData.PenumbraRadian);
                PackedLight.UmbraAndPadding = Vector4(LightData.UmbraRadian, 0.0f, 0.0f, 0.0f);
                PackedLights.push_back(PackedLight);
                break;
            }
            default:
                break;
            }
        }
        const uint32 NumPackedLights = (uint32)PackedLights.size();
        if (PackedLights.empty())
        {
            PackedLights.push_back(jHWRTDIPackedLight());
        }
        const uint32 PackedLightCount = (uint32)PackedLights.size();
        const uint64 PackedLightBufferSize = (uint64)sizeof(jHWRTDIPackedLight) * (uint64)PackedLightCount;
        PackedLightBuffer = g_rhi->CreateStructuredBuffer(PackedLightBufferSize, 0, sizeof(jHWRTDIPackedLight), EBufferCreateFlag::UAV
            , EResourceLayout::GENERAL, PackedLights.data(), PackedLightBufferSize, jNameStatic("HWRTDI_PackedLightBuffer"));
        check(PackedLightBuffer);

        SceneCB.NumLights = NumPackedLights;

        auto SceneUniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_SceneData"), &SceneCB, sizeof(SceneCB));

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

        jTexture* EnvTexture = jSceneRenderTarget::CubeEnvMap2 ? jSceneRenderTarget::CubeEnvMap2 : GWhiteCubeTexture.get();

        jHWRTDIGlobalParameters GlobalParameters;
        GlobalParameters.Scene.Buffer = RaytracingScene->TLASBufferPtr.get();
        GlobalParameters.RenderTarget.Texture = HWRTDIOutput;
        GlobalParameters.g_sceneCB.Buffer = SceneUniformBuffer;
        GlobalParameters.DefaultSamplerState.SamplerState = SamplerState;
        GlobalParameters.EnvTexture.Texture = EnvTexture;
        GlobalParameters.LightBuffer.Buffer = PackedLightBuffer.get();
        jShaderParameterSet::BuildShaderBindings(GlobalParameters, BindingShaderStageFlag, ShaderBindingArray, ResourceInlineAllocator);

        std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

        jHWRTDIBindlessParameters BindlessParameters;
        BindlessParameters.VertexIndexOffsetArray.Buffers = VertexAndIndexOffsetBuffers;
        BindlessParameters.IndexBindlessArray.Buffers = IndexBuffers;
        BindlessParameters.RenderObjParamArray.Buffers = RenderObjectBuffers;
        BindlessParameters.VerticesBindlessArray.Buffers = VertexBuffers;
        BindlessParameters.MaterialInstanceArray.Buffers = MaterialInstanceBuffers;
        BindlessParameters.AlbedoTextureArray.Textures = AlbedoTextures;
        BindlessParameters.NormalTextureArray.Textures = NormalTextures;
        BindlessParameters.RMTextureArray.Textures = RMTextures;
        BindlessParameters.AlbedoSamplerArray.SamplerStates = AlbedoSamplerStates;
        BindlessParameters.NormalSamplerArray.SamplerStates = NormalSamplerStates;
        BindlessParameters.RMSamplerArray.SamplerStates = RMSamplerStates;
        std::vector<std::shared_ptr<jShaderBindingInstance>> BindlessShaderBindingInstances =
            jShaderBindlessSet::CreateShaderBindingInstances(BindlessParameters, BindingShaderStageFlag, jShaderBindingInstanceType::SingleFrame);

        jShaderBindingLayoutArray GlobalShaderBindingLayoutArray;
        GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);
        for (const auto& BindlessShaderBindingInstance : BindlessShaderBindingInstances)
        {
            GlobalShaderBindingLayoutArray.Add(BindlessShaderBindingInstance->ShaderBindingsLayouts);
        }

        jShaderBindingInstanceArray ShaderBindingInstanceArray;
        ShaderBindingInstanceArray.Add(GlobalShaderBindingInstance.get());
        for (const auto& BindlessShaderBindingInstance : BindlessShaderBindingInstances)
        {
            ShaderBindingInstanceArray.Add(BindlessShaderBindingInstance.get());
        }

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

        g_rhi->TransitionLayout(CmdBuffer, PackedLightBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(CmdBuffer, HWRTDIOutput, EResourceLayout::UAV);

        if (InUseInlineRayQuery)
        {
            jShaderHWRTDIInlineRayQueryComputeShader::ShaderPermutation InlinePermutation;
            InlinePermutation.SetIndex<jShaderHWRTDIInlineRayQueryComputeShader::USE_SURFEL_GI>(0);
            InlinePermutation.SetIndex<jShaderHWRTDIInlineRayQueryComputeShader::USE_BINDLESS_RESOURCE>(1);
            jShader* InlineComputeShader = jShaderHWRTDIInlineRayQueryComputeShader::CreateShader(InlinePermutation);
            jPipelineStateInfo* InlineComputePipelineState = g_rhi->CreateComputePipelineStateInfo(InlineComputeShader, GlobalShaderBindingLayoutArray, nullptr);
            InlineComputePipelineState->Bind(InRenderer->RenderFrameContextPtr);
            g_rhi->BindComputeShaderBindingInstances(CmdBuffer, InlineComputePipelineState, ShaderBindingInstanceCombiner, 0);

            const uint32 GroupX = ((uint32)SCR_WIDTH + 7u) / 8u;
            const uint32 GroupY = ((uint32)SCR_HEIGHT + 7u) / 8u;
            g_rhi->DispatchCompute(InRenderer->RenderFrameContextPtr, GroupX, GroupY, 1);
        }
        else
        {
            std::vector<jRaytracingPipelineShader> RaytracingShaders;
            BuildHWRTDIRaytracingShaders(RaytracingShaders);

            jRaytracingPipelineData RaytracingPipelineData;
            RaytracingPipelineData.MaxAttributeSize = 2 * sizeof(float);
            RaytracingPipelineData.MaxPayloadSize = sizeof(Vector4);
            RaytracingPipelineData.MaxTraceRecursionDepth = 2;
            auto RaytracingPipelineState = g_rhi->CreateRaytracingPipelineStateInfo(RaytracingShaders, RaytracingPipelineData, GlobalShaderBindingLayoutArray, nullptr);

            g_rhi->BindRaytracingShaderBindingInstances(CmdBuffer, RaytracingPipelineState, ShaderBindingInstanceCombiner, 0);
            RaytracingPipelineState->Bind(InRenderer->RenderFrameContextPtr);

            jRaytracingDispatchData TracingData;
            TracingData.Width = SCR_WIDTH;
            TracingData.Height = SCR_HEIGHT;
            TracingData.Depth = 1;
            TracingData.PipelineState = RaytracingPipelineState;
            g_rhi->DispatchRay(InRenderer->RenderFrameContextPtr, TracingData);
        }

        g_rhi->TransitionLayout(CmdBuffer, HWRTDIOutput, EResourceLayout::SHADER_READ_ONLY);

        CompositeHWRTDIResult(InRenderer->RenderFrameContextPtr, HWRTDIOutput);
        return true;
    }
}

bool jRenderer::IsUseHWRTDirectLighting() const
{
    return gOptions.UseHWRTDirectLighting
        && gOptions.UseRaytracing
        && GSupportRaytracing
        && RenderFrameContextPtr
        && RenderFrameContextPtr->RaytracingScene
        && RenderFrameContextPtr->RaytracingScene->IsValid()
        && !RenderFrameContextPtr->UseForwardRenderer;
}

bool jRenderer::HWRTDirectLightingPass()
{
    if (!IsUseHWRTDirectLighting())
        return false;

    const int32 ResolvedMode = ResolveHWRTDirectLightingMode(gOptions.HWRTDirectLightingMode);
    if (ResolvedMode != gOptions.HWRTDirectLightingMode)
    {
        gOptions.HWRTDirectLightingMode = ResolvedMode;
    }

    if (ResolvedMode == HWRTDI_Mode_InlineRayQuery)
    {
        return HWRTInlineDirectLightingPass();
    }

    return HWRTDispatchRaysDirectLightingPass();
}

bool jRenderer::HWRTDispatchRaysDirectLightingPass()
{
    if (!IsUseHWRTDirectLighting())
        return false;

    return RunHWRTDirectLightingPass(this, false);
}

bool jRenderer::HWRTInlineDirectLightingPass()
{
    if (!IsUseHWRTDirectLighting())
        return false;

    if (!GSupportInlineRaytracing)
    {
        return HWRTDispatchRaysDirectLightingPass();
    }

    return RunHWRTDirectLightingPass(this, true);
}
