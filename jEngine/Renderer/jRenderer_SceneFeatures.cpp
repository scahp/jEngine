#include "pch.h"
#include "jRenderer.h"
#include "Features/jSceneFeatureSettings.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jRHIUtil.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Shader/jShaderParameterSet.h"
#include "Profiler/jPerformanceProfile.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSceneFeatureViewData)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, VP)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, EyeWorld)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SizeAndInvSize)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSceneEnvironmentData)
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, RadianceSH, 9)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, IntensityAndPadding)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSceneReflectionData)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Plane)
    // thickness, normal alignment threshold, roughness threshold, intensity.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, MaskAndIntensity)
    // hole filling, viewport edge fade, roughness LOD multiplier, unused.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, ResolveParams)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, FallbackRadianceAndUseCube)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSceneAtmosphereData)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, RayleighAndScaleHeight)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, MieAndScaleHeight)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SunDirectionAndG)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SolarIrradianceAndIntensity)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, DistanceParams)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SampleCounts)
END_SHADER_UNIFORM_BUFFER_STRUCT()

static_assert(sizeof(jSceneFeatureViewData) == 160);
static_assert(sizeof(jSceneEnvironmentData) == 160);
static_assert(sizeof(jSceneReflectionData) == 64);
static_assert(sizeof(jSceneAtmosphereData) == 96);

BEGIN_SHADER_PARAMETER_SET(jSceneEnvironmentParameters)
    SHADER_UNIFORM_BUFFER(jSceneFeatureViewData, FeatureView)
    SHADER_UNIFORM_BUFFER(jSceneEnvironmentData, Environment)
    SHADER_TEXTURE2D_SRV(SceneColor)
    SHADER_TEXTURE2D_SRV(SceneDepth)
    SHADER_TEXTURE2D_SRV(SceneNormal)
    SHADER_TEXTURE2D_SRV(SceneAlbedo)
    SHADER_TEXTURE2D_SRV(SceneMaterial)
    SHADER_TEXTURE2D_SRV(SceneAmbientOcclusion)
    SHADER_RW_TEXTURE2D(Result)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSceneReflectionClearParameters)
    SHADER_UNIFORM_BUFFER(jSceneFeatureViewData, FeatureView)
    SHADER_RW_TEXTURE2D_UINT(Projection)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSceneEnvironmentSubsurfaceParameters)
    SHADER_UNIFORM_BUFFER(jSceneFeatureViewData, FeatureView)
    SHADER_UNIFORM_BUFFER(jSceneEnvironmentData, Environment)
    SHADER_TEXTURE2D_SRV(SceneColor)
    SHADER_TEXTURE2D_SRV(SceneDepth)
    SHADER_TEXTURE2D_SRV(SceneNormal)
    SHADER_TEXTURE2D_SRV(SceneAlbedo)
    SHADER_TEXTURE2D_SRV(SceneMaterial)
    SHADER_TEXTURE2D_SRV(SceneAmbientOcclusion)
    SHADER_RW_TEXTURE2D(Result)
    SHADER_TEXTURE2D_SRV(SubsurfaceDiffuse)
    SHADER_RW_TEXTURE2D(SubsurfaceDiffuseResult)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSceneReflectionProjectionParameters)
    SHADER_UNIFORM_BUFFER(jSceneFeatureViewData, FeatureView)
    SHADER_UNIFORM_BUFFER(jSceneReflectionData, Reflection)
    SHADER_TEXTURE2D_SRV(SceneDepth)
    SHADER_RW_TEXTURE2D_UINT(Projection)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSceneReflectionResolveParameters)
    SHADER_UNIFORM_BUFFER(jSceneFeatureViewData, FeatureView)
    SHADER_UNIFORM_BUFFER(jSceneReflectionData, Reflection)
    SHADER_TEXTURE2D_SRV(SceneColor)
    SHADER_TEXTURE2D_SRV(SceneDepth)
    SHADER_TEXTURE2D_SRV(SceneNormal)
    SHADER_TEXTURE2D_SRV(SceneAlbedo)
    SHADER_TEXTURE2D_SRV(SceneMaterial)
    SHADER_RW_TEXTURE2D_UINT(Projection)
    SHADER_RW_TEXTURE2D(Result)
END_SHADER_PARAMETER_SET()

// Keep the optional environment resource out of the fallback variant's actual
// descriptor layout, rather than bind an absent or incorrectly typed null cube.
BEGIN_SHADER_PARAMETER_SET(jSceneReflectionResolveCubeParameters)
    SHADER_UNIFORM_BUFFER(jSceneFeatureViewData, FeatureView)
    SHADER_UNIFORM_BUFFER(jSceneReflectionData, Reflection)
    SHADER_TEXTURE2D_SRV(SceneColor)
    SHADER_TEXTURE2D_SRV(SceneDepth)
    SHADER_TEXTURE2D_SRV(SceneNormal)
    SHADER_TEXTURE2D_SRV(SceneAlbedo)
    SHADER_TEXTURE2D_SRV(SceneMaterial)
    SHADER_RW_TEXTURE2D_UINT(Projection)
    SHADER_RW_TEXTURE2D(Result)
    SHADER_TEXTURECUBE(FallbackEnvironment)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSceneAtmosphereParameters)
    SHADER_UNIFORM_BUFFER(jSceneFeatureViewData, FeatureView)
    SHADER_UNIFORM_BUFFER(jSceneAtmosphereData, Atmosphere)
    SHADER_TEXTURE2D_SRV(SceneColor)
    SHADER_TEXTURE2D_SRV(SceneDepth)
    SHADER_RW_TEXTURE2D(Result)
END_SHADER_PARAMETER_SET()

namespace
{
enum class SceneFeaturePass : uint32
{
    Environment = 1,
    ReflectionClear = 2,
    ReflectionProjection = 3,
    ReflectionResolve = 4,
    Atmosphere = 5
};

template<typename T>
std::shared_ptr<IUniformBufferBlock> FeatureUniform(const char* name, const T& data)
{
    auto buffer = g_rhi->CreateUniformBufferBlock(jName(name), jLifeTimeType::OneFrame, sizeof(T));
    if (buffer) buffer->UpdateBufferData(&data, sizeof(T));
    return buffer;
}

template<typename T>
bool DispatchFeature(const std::shared_ptr<jRenderFrameContext>& frame, const char* name,
    SceneFeaturePass pass, const T& parameters, uint32 width, uint32 height, const char* defines = "")
{
    jShaderInfo info;
    info.SetName(jName(name));
    // Nested entry directories are not supported by the current runtime include
    // loader. All passes compile through this root entry with a distinct define.
    info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/scene_features_cs.hlsl"));
    info.SetEntryPoint(jNameStatic("main"));
    info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    const std::string passDefines = "#define JE_SCENE_FEATURE_PASS " + std::to_string(static_cast<uint32>(pass)) + "\n" + defines;
    info.SetPreProcessors(jName(passDefines));
    jShaderParameterSet::AppendToShaderInfo<T>(info, 0);
    auto* shader = g_rhi->CreateShader(info);
    if (!shader || !shader->GetCompiledShader())
    {
        std::cerr << "SceneFeatures: shader creation failed for " << name << '\n';
        return false;
    }
    const auto binding = jShaderParameterSet::CreateShaderBindingInstance(parameters,
        EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);
    if (!binding)
    {
        std::cerr << "SceneFeatures: resource binding failed for " << name << '\n';
        return false;
    }
    jShaderBindingInstanceGroup group;
    group.Add(binding);
    auto* pipeline = g_rhi->CreateComputePipelineStateInfo(shader, group.GetLayoutArray(), {});
    if (!pipeline)
    {
        std::cerr << "SceneFeatures: pipeline creation failed for " << name << '\n';
        return false;
    }
    pipeline->Bind(frame);
    g_rhi->BindComputeShaderBindingInstances(frame->GetActiveCommandBuffer(), pipeline, group.GetCombiner(), 0);
    g_rhi->DispatchCompute(frame, (width + 7) / 8, (height + 7) / 8, 1);
    return true;
}

void CopyFeatureToSceneColor(const std::shared_ptr<jRenderFrameContext>& frame,
    const std::shared_ptr<jRenderTarget>& sceneColor, jTexture* source)
{
    auto* command = frame->GetActiveCommandBuffer();
    g_rhi->TransitionLayout(command, source, EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(command, sceneColor->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
    jRHIUtil::DrawFullScreen(frame, sceneColor,
        [source](const std::shared_ptr<jRenderFrameContext>&, jShaderBindingArray& bindings,
            jShaderBindingResourceInlineAllocator& allocator)
        {
            jRHIUtil::BuildSingleTextureFragmentBindings(source,
                TSamplerStateInfo<ETextureFilter::NEAREST, ETextureFilter::NEAREST>::Create(), bindings, allocator);
        }, [](const std::shared_ptr<jRenderFrameContext>&)
        {
            jShaderInfo info;
            info.SetName(jNameStatic("SceneFeaturesCopyPS"));
            info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
            info.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            jRHIUtil::AppendSingleTextureFragmentShaderInfo(info);
            return g_rhi->CreateShader(info);
        });
}

template<typename T>
void SetReflectionResolveParameters(T& p, const std::shared_ptr<IUniformBufferBlock>& view,
    const std::shared_ptr<IUniformBufferBlock>& reflection, const jSceneRenderTarget& scene,
    jTexture* source, jTexture* projection, jTexture* result)
{
    p.FeatureView.Buffer = view;
    p.Reflection.Buffer = reflection;
    p.SceneColor = {source};
    p.SceneDepth = {scene.DepthPtr->GetTexture()};
    p.SceneNormal = {scene.GBuffer[0]->GetTexture()};
    p.SceneAlbedo = {scene.GBuffer[1]->GetTexture()};
    p.SceneMaterial = {scene.GBuffer[2]->GetTexture()};
    p.Projection = {projection};
    p.Result = {result};
}
}

void jRenderer::SceneFeaturesPass(const jSceneFeatures::Settings& input)
{
#ifdef _WIN32
    char diagnosticSetting[2]{};
    const bool diagnostics = GetEnvironmentVariableA("JENGINE_TEST_SCENE_FEATURES", diagnosticSetting,
        sizeof(diagnosticSetting)) == 1 && diagnosticSetting[0] == '1';
#else
    const char* diagnosticSetting = std::getenv("JENGINE_TEST_SCENE_FEATURES");
    const bool diagnostics = diagnosticSetting && diagnosticSetting[0] == '1';
#endif
    jSceneFeatures::Settings settings;
    std::string error;
    if (!jSceneFeatures::ValidateSettings(input, settings, error))
    {
        std::cerr << "SceneFeatures: invalid frame snapshot: " << error << '\n';
        return;
    }
    if (!RenderFrameContextPtr || !RenderFrameContextPtr->SceneRenderTargetPtr || !View.Camera)
        return;
    const auto& frame = RenderFrameContextPtr;
    auto& scene = *frame->SceneRenderTargetPtr;
    if (!scene.ColorPtr || !scene.DepthPtr || scene.ColorPtr->Info.SampleCount != EMSAASamples::COUNT_1)
        return;
    const uint32 width = static_cast<uint32>(scene.ColorPtr->Info.Width);
    const uint32 height = static_cast<uint32>(scene.ColorPtr->Info.Height);
    if (!width || !height) return;

    bool storedDeferred = !frame->UseForwardRenderer && std::size(scene.GBuffer) >= 5;
    for (const auto& target : scene.GBuffer)
        storedDeferred = storedDeferred && target && !target->Info.IsMemoryless &&
            target->Info.SampleCount == EMSAASamples::COUNT_1;
    const bool environmentEnabled = storedDeferred && settings.Environment.Enabled && settings.Environment.Intensity > 0.0f;
    const bool reflectionEnabled = storedDeferred && settings.Reflection.Enabled && settings.Reflection.Intensity > 0.0f &&
        width <= 16384 && height <= 16384;
    const bool atmosphereEnabled = settings.Atmosphere.Enabled && settings.Atmosphere.Intensity > 0.0f;
    if (diagnostics)
    {
        bool memoryless = false;
        uint32 missingBuffers = 0;
        for (const auto& target : scene.GBuffer)
        {
            if (!target) ++missingBuffers;
            else memoryless = memoryless || target->Info.IsMemoryless;
        }
        std::cout << "SCENE_FEATURES_INPUT frame=" << g_rhi->GetCurrentFrameNumber()
            << " forward=" << frame->UseForwardRenderer << " samples=" << static_cast<uint32>(scene.ColorPtr->Info.SampleCount)
            << " gbuffer_count=" << std::size(scene.GBuffer) << " missing=" << missingBuffers
            << " memoryless=" << memoryless << " stored_deferred=" << storedDeferred
            << " sh_requested=" << settings.Environment.Enabled << " sh_intensity=" << settings.Environment.Intensity
            << " ppr_requested=" << settings.Reflection.Enabled << " ppr_intensity=" << settings.Reflection.Intensity
            << " atmosphere_requested=" << settings.Atmosphere.Enabled
            << " selected=" << environmentEnabled << ',' << reflectionEnabled << ',' << atmosphereEnabled << '\n';
    }
    if (!environmentEnabled && !reflectionEnabled && !atmosphereEnabled) return;

    SCOPE_GPU_PROFILE(frame, SceneFeatures);
    DEBUG_EVENT_WITH_COLOR(frame, "SceneFeatures", Vector4(0.15f, 0.5f, 0.85f, 1.0f));
    const jRenderTargetInfo scratchInfo = {
        .Type = ETextureType::TEXTURE_2D, .Format = ETextureFormat::RGBA16F,
        .Width = static_cast<int32>(width), .Height = static_cast<int32>(height),
        .SampleCount = EMSAASamples::COUNT_1,
        .TextureCreateFlag = ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("SceneFeaturesHDR")};
    auto source = jRenderTargetPool::GetRenderTargetForOneFrame(scratchInfo);
    auto result = jRenderTargetPool::GetRenderTargetForOneFrame(scratchInfo);
    if (!source || !result || !source->GetTexture() || !result->GetTexture()) return;

    jSceneFeatureViewData viewData{};
    viewData.InvVP = View.Camera->GetInverseViewProjectionMatrix();
    viewData.VP = View.Camera->GetViewProjectionMatrix();
    // w distinguishes perspective eye rays from parallel orthographic rays.
    viewData.EyeWorld = Vector4(View.Camera->Pos, View.Camera->IsPerspectiveProjection ? 1.0f : 0.0f);
    viewData.SizeAndInvSize = Vector4(static_cast<float>(width), static_cast<float>(height),
        1.0f / width, 1.0f / height);
    const auto view = FeatureUniform("SceneFeatureView", viewData);
    if (!view) return;

    // R11G11B10 scene color is only read as an SRV and written as a framebuffer
    // attachment. All feature UAV stores use separate RGBA16F ping/pong images.
    // Existing OneFrame pool and ring/descriptor retirement own GPU lifetimes.
    jRHIUtil::CopyTexture2D(frame, source->GetTexture(), scene.ColorPtr->GetTexture());
    auto* command = frame->GetActiveCommandBuffer();
    g_rhi->TransitionLayout(command, scene.DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    if (storedDeferred)
        for (const auto& target : scene.GBuffer)
            g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    auto prepareColor = [&]
    {
        g_rhi->TransitionLayout(command, source->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(command, result->GetTexture(), EResourceLayout::UAV);
    };
    bool environmentRecorded = false, projectionRecorded = false, reflectionRecorded = false, atmosphereRecorded = false;

    if (environmentEnabled)
    {
        jSceneEnvironmentData data{};
        for (std::size_t i = 0; i < settings.Environment.Radiance.size(); ++i)
        {
            const auto& c = settings.Environment.Radiance[i];
            data.RadianceSH[i] = Vector4(c[0], c[1], c[2], 0.0f);
        }
        // The caller supplies effective intensity after its actual-frame diffuse
        // GI policy. No light/GI options or shared surface resources are mutated.
        data.IntensityAndPadding = Vector4(settings.Environment.Intensity, 0.0f, 0.0f, 0.0f);
        const auto uniform = FeatureUniform("SceneEnvironment", data);
        if (uniform)
        {
            prepareColor();
            jSceneEnvironmentParameters p;
            p.FeatureView.Buffer = view;
            p.Environment.Buffer = uniform;
            p.SceneColor = {source->GetTexture()}; p.SceneDepth = {scene.DepthPtr->GetTexture()};
            p.SceneNormal = {scene.GBuffer[0]->GetTexture()}; p.SceneAlbedo = {scene.GBuffer[1]->GetTexture()};
            p.SceneMaterial = {scene.GBuffer[2]->GetTexture()}; p.SceneAmbientOcclusion = {scene.GBuffer[4]->GetTexture()};
            p.Result = {result->GetTexture()};
            if (frame->UseSubsurface && scene.DiffuseLighting)
            {
                auto info = scene.DiffuseLighting->Info;
                info.TextureCreateFlag |= ETextureCreateFlag::UAV;
                info.ResourceName = jNameStatic("SubsurfaceDiffuseWithEnvironment");
                auto diffuse = jRenderTargetPool::GetRenderTarget(info);
                if (diffuse)
                {
                    jSceneEnvironmentSubsurfaceParameters s;
                    s.FeatureView = p.FeatureView; s.Environment = p.Environment;
                    s.SceneColor = p.SceneColor; s.SceneDepth = p.SceneDepth; s.SceneNormal = p.SceneNormal;
                    s.SceneAlbedo = p.SceneAlbedo; s.SceneMaterial = p.SceneMaterial;
                    s.SceneAmbientOcclusion = p.SceneAmbientOcclusion; s.Result = p.Result;
                    s.SubsurfaceDiffuse = {scene.DiffuseLighting->GetTexture()};
                    s.SubsurfaceDiffuseResult = {diffuse->GetTexture()};
                    g_rhi->TransitionLayout(command, scene.DiffuseLighting->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(command, diffuse->GetTexture(), EResourceLayout::UAV);
                    environmentRecorded = DispatchFeature(frame, "SceneEnvironmentSubsurfaceCS",
                        SceneFeaturePass::Environment, s, width, height, "#define JE_ENVIRONMENT_SUBSURFACE 1\n");
                    if (environmentRecorded)
                    {
                        scene.MultisampleTargets.push_back(scene.DiffuseLighting);
                        scene.DiffuseLighting = diffuse;
                    }
                    else diffuse->Return();
                }
            }
            else environmentRecorded = DispatchFeature(frame, "SceneEnvironmentCS",
                SceneFeaturePass::Environment, p, width, height);
            if (environmentRecorded) std::swap(source, result);
        }
    }

    if (reflectionEnabled)
    {
        const auto& s = settings.Reflection;
        auto projection = jRenderTargetPool::GetRenderTargetForOneFrame({
            .Type = ETextureType::TEXTURE_2D, .Format = ETextureFormat::R32UI,
            .Width = static_cast<int32>(width), .Height = static_cast<int32>(height),
            .SampleCount = EMSAASamples::COUNT_1, .TextureCreateFlag = ETextureCreateFlag::UAV,
            .ResourceName = jNameStatic("PixelProjectedReflectionOffsets")});
        jTexture* cube = jSceneRenderTarget::FilteredEnvMap2 ? jSceneRenderTarget::FilteredEnvMap2 : jSceneRenderTarget::CubeEnvMap2;
        if (cube && cube->Type != ETextureType::TEXTURE_CUBE) cube = nullptr;
        jSceneReflectionData data{};
        data.Plane = Vector4(s.Plane[0], s.Plane[1], s.Plane[2], s.Plane[3]);
        data.MaskAndIntensity = Vector4(s.PlaneThickness, s.NormalThreshold, s.RoughnessThreshold, s.Intensity);
        data.ResolveParams = Vector4(s.FillHoles ? 1.0f : 0.0f, s.EdgeFade, s.FallbackRoughnessLod, 0.0f);
        data.FallbackRadianceAndUseCube = Vector4(s.FallbackRadiance[0], s.FallbackRadiance[1], s.FallbackRadiance[2], cube ? 1.0f : 0.0f);
        const auto uniform = FeatureUniform("SceneReflection", data);
        if (projection && projection->GetTexture() && uniform)
        {
            auto* offsets = projection->GetTexture();
            g_rhi->TransitionLayout(command, offsets, EResourceLayout::UAV);
            jSceneReflectionClearParameters clear;
            clear.FeatureView.Buffer = view; clear.Projection = {offsets};
            const bool cleared = DispatchFeature(frame, "PPRClearCS",
                SceneFeaturePass::ReflectionClear, clear, width, height);
            if (cleared)
            {
                g_rhi->UAVBarrier(command, offsets);
                jSceneReflectionProjectionParameters projectionParameters;
                projectionParameters.FeatureView.Buffer = view; projectionParameters.Reflection.Buffer = uniform;
                projectionParameters.SceneDepth = {scene.DepthPtr->GetTexture()}; projectionParameters.Projection = {offsets};
                projectionRecorded = DispatchFeature(frame, "PPRProjectionCS",
                    SceneFeaturePass::ReflectionProjection, projectionParameters, width, height);
            }
            if (projectionRecorded)
            {
                g_rhi->UAVBarrier(command, offsets);
                prepareColor();
                if (cube)
                {
                    g_rhi->TransitionLayout(command, cube, EResourceLayout::SHADER_READ_ONLY);
                    jSceneReflectionResolveCubeParameters p;
                    SetReflectionResolveParameters(p, view, uniform, scene, source->GetTexture(), offsets, result->GetTexture());
                    p.FallbackEnvironment = {cube, TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR,
                        ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE>::Create()};
                    reflectionRecorded = DispatchFeature(frame, "PPRResolveCubeCS",
                        SceneFeaturePass::ReflectionResolve, p, width, height, "#define PPR_USE_CUBE 1\n");
                }
                else
                {
                    jSceneReflectionResolveParameters p;
                    SetReflectionResolveParameters(p, view, uniform, scene, source->GetTexture(), offsets, result->GetTexture());
                    reflectionRecorded = DispatchFeature(frame, "PPRResolveCS",
                        SceneFeaturePass::ReflectionResolve, p, width, height);
                }
                if (reflectionRecorded) std::swap(source, result);
            }
        }
    }

    if (atmosphereEnabled)
    {
        const auto& s = settings.Atmosphere;
        jSceneAtmosphereData data{};
        data.RayleighAndScaleHeight = Vector4(s.RayleighScattering[0], s.RayleighScattering[1], s.RayleighScattering[2], s.RayleighScaleHeightMeters);
        data.MieAndScaleHeight = Vector4(s.MieScattering[0], s.MieScattering[1], s.MieScattering[2], s.MieScaleHeightMeters);
        Vector sun(s.SunDirection[0], s.SunDirection[1], s.SunDirection[2]);
        Vector irradiance(s.SolarIrradiance[0], s.SolarIrradiance[1], s.SolarIrradiance[2]);
        if (s.UseSceneDirectionalLight)
        {
            for (const auto& light : View.Lights)
            {
                if (!light.Light || light.Light->Type != ELightType::DIRECTIONAL) continue;
                const auto& lightData = static_cast<const jDirectionalLight*>(light.Light)->GetLightData();
                if (lightData.Direction.LengthSQ() > 1.0e-12f) sun = -lightData.Direction.GetNormalize();
                irradiance *= lightData.Color;
                break;
            }
        }
        data.SunDirectionAndG = Vector4(sun, s.MieAnisotropy);
        data.SolarIrradianceAndIntensity = Vector4(irradiance, s.Intensity);
        data.DistanceParams = Vector4(s.MetersPerWorldUnit, s.GroundHeight, s.AtmosphereHeightMeters, s.MaxDistanceMeters);
        data.SampleCounts = Vector4(static_cast<float>(s.ViewSamples), static_cast<float>(s.LightSamples), 0.0f, 0.0f);
        const auto uniform = FeatureUniform("SceneAtmosphere", data);
        if (uniform)
        {
            prepareColor();
            jSceneAtmosphereParameters p;
            p.FeatureView.Buffer = view; p.Atmosphere.Buffer = uniform;
            p.SceneColor = {source->GetTexture()}; p.SceneDepth = {scene.DepthPtr->GetTexture()}; p.Result = {result->GetTexture()};
            atmosphereRecorded = DispatchFeature(frame, "SceneAtmosphereCS",
                SceneFeaturePass::Atmosphere, p, width, height);
            if (atmosphereRecorded) std::swap(source, result);
        }
    }

    CopyFeatureToSceneColor(frame, scene.ColorPtr, source->GetTexture());
    if (diagnostics)
        std::cout << "SCENE_FEATURES_RECORDED frame=" << g_rhi->GetCurrentFrameNumber()
            << " size=" << width << 'x' << height << " sh=" << environmentRecorded
            << " ppr_projection=" << projectionRecorded << " ppr_resolve=" << reflectionRecorded
            << " atmosphere=" << atmosphereRecorded << '\n';
}

namespace
{
template<typename T>
void WriteFeatureLayout(std::ostream& out, bool& first)
{
    if (!first) out << ',';
    first = false;
    out << '"' << T::GetShaderUniformBufferTypeName() << "\":{\"size\":" << sizeof(T) << ",\"fields\":{";
    bool firstField = true;
    for (const auto& field : T::GetShaderUniformBufferFieldMembers())
    {
        if (!firstField) out << ',';
        firstField = false;
        out << '"' << field.Name << "\":" << field.ByteOffset;
    }
    out << "}}";
}
}

// CPU-only export uses the exact parameter metadata of the production passes.
int ExportSceneFeatureShaderFixtures(const char* directory, bool vulkan)
{
    try
    {
        gAPIType = vulkan ? EAPIType::Vulkan : EAPIType::DX12;
        const std::filesystem::path target(directory);
        std::filesystem::create_directories(target);
        const std::string flags = "#define COMPUTE_SHADER 1\n#define PIXEL_SHADER 0\n";
        auto write = [&](const char* name, SceneFeaturePass pass, const std::string& declarations, const char* defines = "")
        {
            std::ofstream file(target / (std::string(name) + ".hlsl"));
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file << flags << "#define JE_SCENE_FEATURE_PASS " << static_cast<uint32>(pass) << '\n' << defines
                << "#include \"common.hlsl\"\n" << declarations << "\n#include \"scene_features_cs.hlsl\"\n";
        };
        write("scene-environment", SceneFeaturePass::Environment, jShaderParameterSet::GenerateHLSL<jSceneEnvironmentParameters>(0));
        write("scene-ppr-clear", SceneFeaturePass::ReflectionClear, jShaderParameterSet::GenerateHLSL<jSceneReflectionClearParameters>(0));
        write("scene-ppr-projection", SceneFeaturePass::ReflectionProjection, jShaderParameterSet::GenerateHLSL<jSceneReflectionProjectionParameters>(0));
        write("scene-ppr-resolve", SceneFeaturePass::ReflectionResolve, jShaderParameterSet::GenerateHLSL<jSceneReflectionResolveParameters>(0));
        write("scene-ppr-resolve-cube", SceneFeaturePass::ReflectionResolve, jShaderParameterSet::GenerateHLSL<jSceneReflectionResolveCubeParameters>(0), "#define PPR_USE_CUBE 1\n");
        write("scene-atmosphere", SceneFeaturePass::Atmosphere, jShaderParameterSet::GenerateHLSL<jSceneAtmosphereParameters>(0));
        std::ofstream layout(target / "scene-features-cpu-layout.json");
        layout.exceptions(std::ios::badbit | std::ios::failbit);
        layout << '{';
        bool first = true;
        WriteFeatureLayout<jSceneFeatureViewData>(layout, first);
        WriteFeatureLayout<jSceneEnvironmentData>(layout, first);
        WriteFeatureLayout<jSceneReflectionData>(layout, first);
        WriteFeatureLayout<jSceneAtmosphereData>(layout, first);
        layout << "}\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Scene feature shader export failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
