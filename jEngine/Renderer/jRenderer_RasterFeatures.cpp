#include "pch.h"
#include "jRenderer.h"
#include "jSceneRenderTargets.h"
#include "jSubsurfacePass.h"
#include "jTextureSubsurfacePass.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jRenderObject.h"
#include "Material/jMaterial.h"
#include "jRasterFeatureDebug.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jRHIUtil.h"
#include "RHI/jResourceBarrierBatcher.h"
#include "Scene/jCamera.h"
#include "Shader/jDeferredResolveShaderParameters.h"
#include "jOptions.h"
#include <cstdlib>

namespace jRasterFeatureDebug
{
static MultisampleOutput Last;
bool GetLastMultisampleOutput(MultisampleOutput& output)
{
    if (!g_rhi || Last.Sources.empty() || Last.FrameNumber != g_rhi->GetCurrentFrameNumber()) return false;
    output = Last;
    return true;
}
void ReleaseResources() { Last = {}; }
}

namespace
{
void FlushRasterTransitions(const std::shared_ptr<jRenderFrameContext>& frame)
{
#if USE_RESOURCE_BARRIER_BATCHER
    auto* command = frame->GetActiveCommandBuffer();
    if (auto* batcher = g_rhi->GetGlobalBarrierBatcher()) batcher->Flush(command);
    command->FlushBarrierBatch();
#endif
}
template<typename T>
void BindResolveInputs(const std::shared_ptr<jRenderFrameContext>& frame, T& p,
    jShaderInfo& info, jShaderBindingInstanceGroup& bindings)
{
    const auto& scene = *frame->SceneRenderTargetPtr;
    p.SceneColorMS = {scene.ColorPtr->GetTexture()}; p.DepthMS = {scene.DepthPtr->GetTexture()};
    p.NormalMS = {scene.GBuffer[0]->GetTexture()}; p.AlbedoMS = {scene.GBuffer[1]->GetTexture()};
    p.MaterialMS = {scene.GBuffer[2]->GetTexture()}; p.EmissionMS = {scene.GBuffer[3]->GetTexture()};
    p.VisibilityMS = {scene.GBuffer[4]->GetTexture()};
    jShaderParameterSet::AppendToShaderInfo<T>(info, 0);
    bindings.Add(jShaderParameterSet::CreateShaderBindingInstance(p, EShaderAccessStageFlag::FRAGMENT,
        jShaderBindingInstanceType::SingleFrame));
}
}

bool jRenderer::ResolveMultisampleScene()
{
    auto& scene = *RenderFrameContextPtr->SceneRenderTargetPtr;
    jRasterFeatureDebug::ReleaseResources();
    if (scene.ColorPtr->Info.SampleCount == EMSAASamples::COUNT_1) return true;
    auto* command = RenderFrameContextPtr->GetActiveCommandBuffer();
    std::vector<std::shared_ptr<jRenderTarget>> sources = {scene.ColorPtr};
    sources.insert(sources.end(), std::begin(scene.GBuffer), std::end(scene.GBuffer));
    if (RenderFrameContextPtr->UseSubsurface)
    { sources.push_back(scene.SubsurfaceMask); sources.push_back(scene.DiffuseLighting); }
    std::vector<std::shared_ptr<jRenderTarget>> resolved;
    jRenderPassInfo passInfo;
    passInfo.SynchronizeDepthStencil = true;
    jSubpass subpass;
    subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
    for (const auto& source : sources)
    {
        auto info = source->Info;
        info.SampleCount = EMSAASamples::COUNT_1;
        info.IsUseAsSubpassInput = false; info.IsMemoryless = false;
        if (resolved.empty()) info.TextureCreateFlag |= ETextureCreateFlag::UAV;
        info.ResourceName = jNameStatic("DeferredResolvedAttachment");
        auto target = jRenderTargetPool::GetRenderTarget(info);
        if (!target) return false;
        g_rhi->TransitionLayout(command, source->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        subpass.OutputColorAttachments.push_back(static_cast<int32>(resolved.size()));
        passInfo.Attachments.push_back({.RenderTargetPtr = target,
            .LoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_STORE,
            .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
            .InitialLayout = EResourceLayout::COLOR_ATTACHMENT, .FinalLayout = EResourceLayout::COLOR_ATTACHMENT});
        resolved.push_back(target);
    }
    auto depthInfo = scene.DepthPtr->Info;
    depthInfo.SampleCount = EMSAASamples::COUNT_1;
    depthInfo.IsUseAsSubpassInput = false; depthInfo.IsMemoryless = false;
    depthInfo.ResourceName = jNameStatic("DeferredResolvedDepth");
    auto depth = jRenderTargetPool::GetRenderTarget(depthInfo);
    if (!depth) return false;
    g_rhi->TransitionLayout(command, scene.DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(command, depth->GetTexture(), EResourceLayout::DEPTH_STENCIL_ATTACHMENT);
    subpass.OutputDepthAttachment = static_cast<int32>(resolved.size());
    passInfo.Attachments.push_back({.RenderTargetPtr = depth, .LoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_STORE,
        .StencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE, .RTClearValue = jRTClearValue(1.f, 0),
        .InitialLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT, .FinalLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT});
    passInfo.Subpasses.push_back(subpass);
    auto* pass = g_rhi->GetOrCreateRenderPass(passInfo, {0, 0}, {depthInfo.Width, depthInfo.Height});
    jShaderInfo vsInfo, psInfo;
    vsInfo.SetName(jNameStatic("DeferredResolveVS"));
    vsInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/deferred_resolve_vs.hlsl"));
    vsInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
    psInfo.SetName(jNameStatic("DeferredResolvePS"));
    psInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/deferred_resolve_ps.hlsl"));
    psInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
    psInfo.AddPreProcessor("USE_SUBSURFACE", RenderFrameContextPtr->UseSubsurface ? "1" : "0");
    jShaderBindingInstanceGroup bindings;
    if (RenderFrameContextPtr->UseSubsurface)
    {
        jDeferredResolveSubsurfaceParameters p;
        p.SubsurfaceMaskMS = {scene.SubsurfaceMask->GetTexture()}; p.DiffuseMS = {scene.DiffuseLighting->GetTexture()};
        BindResolveInputs(RenderFrameContextPtr, p, psInfo, bindings);
    }
    else { jDeferredResolveParameters p; BindResolveInputs(RenderFrameContextPtr, p, psInfo, bindings); }
    auto* vs = g_rhi->CreateShader(vsInfo); auto* ps = g_rhi->CreateShader(psInfo);
    if (!vs || !ps || !vs->GetCompiledShader() || !ps->GetCompiledShader()) return false;
    auto* raster = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE, EFrontFace::CCW,
        false, 0.f, 0.f, 0.f, 1.f, false, false, EMSAASamples::COUNT_1>::Create();
    auto* depthState = TDepthStencilStateInfo<true, true, ECompareOp::ALWAYS>::Create();
    auto* blend = TBlendingStateInfo<false>::Create();
    jPipelineStateFixedInfo fixed(raster, depthState, blend,
        jViewport(0.f, 0.f, float(depthInfo.Width), float(depthInfo.Height)), jScissor(0, 0, depthInfo.Width, depthInfo.Height), false);
    jGraphicsPipelineShader shaders; shaders.VertexShader = vs; shaders.PixelShader = ps;
    auto* pipeline = g_rhi->CreatePipelineStateInfo(&fixed, shaders, {}, pass, bindings.GetLayoutArray(), nullptr, 0);
    if (!pipeline) return false;
    FlushRasterTransitions(RenderFrameContextPtr);
    if (!pass->BeginRenderPass(command)) return false;
    pipeline->Bind(RenderFrameContextPtr);
    g_rhi->BindGraphicsShaderBindingInstances(command, pipeline, bindings.GetCombiner(), 0);
    g_rhi->DrawArrays(RenderFrameContextPtr, EPrimitiveType::TRIANGLES, 0, 3);
    pass->EndRenderPass();
    if (scene.SkinUV)
    {
        // HDR, normals, material metadata and diffusion already consume eight
        // colour outputs. Resolve UV/identity in a separate one-target pass.
        auto info = scene.SkinUV->Info;
        info.SampleCount = EMSAASamples::COUNT_1;
        info.ResourceName = jNameStatic("SkinSurfaceUVResolved");
        auto target = jRenderTargetPool::GetRenderTarget(info);
        if (!target) return false;
        auto* uv = scene.SkinUV->GetTexture();
        auto* sourceDepth = scene.DepthPtr->GetTexture();
        g_rhi->TransitionLayout(command, uv, EResourceLayout::SHADER_READ_ONLY);
        jRHIUtil::DrawFullScreen(RenderFrameContextPtr, target,
            [uv, sourceDepth](const std::shared_ptr<jRenderFrameContext>&, jShaderBindingArray& array,
                jShaderBindingResourceInlineAllocator& allocator)
            {
                jSkinUVResolveParameters p; p.SkinUVMS = {uv}; p.DepthMS = {sourceDepth};
                jShaderParameterSet::BuildShaderBindings(p, EShaderAccessStageFlag::FRAGMENT, array, allocator);
            }, [](const std::shared_ptr<jRenderFrameContext>&)
            {
                jShaderInfo info; info.SetName(jNameStatic("SkinUVResolvePS"));
                info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/skin_uv_resolve_ps.hlsl"));
                info.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                jShaderParameterSet::AppendToShaderInfo<jSkinUVResolveParameters>(info, 0);
                return g_rhi->CreateShader(info);
            });
        scene.SkinIntermediateTargets.push_back(scene.SkinUV);
        scene.SkinUV = target;
    }
    if (const char* test = std::getenv("JENGINE_TEST_RASTER_FEATURES"); test && test[0] == '1')
    {
        jRasterFeatureDebug::Last.FrameNumber = g_rhi->GetCurrentFrameNumber();
        jRasterFeatureDebug::Last.Sources = sources;
        jRasterFeatureDebug::Last.Resolved = resolved;
        jRasterFeatureDebug::Last.SourceDepth = scene.DepthPtr;
        jRasterFeatureDebug::Last.ResolvedDepth = depth;
    }
    sources.push_back(scene.DepthPtr);
    scene.MultisampleTargets = std::move(sources);
    scene.ColorPtr = resolved[0];
    for (std::size_t i = 0; i < std::size(scene.GBuffer); ++i) scene.GBuffer[i] = resolved[i + 1];
    scene.DepthPtr = depth;
    if (RenderFrameContextPtr->UseSubsurface) { scene.SubsurfaceMask = resolved[6]; scene.DiffuseLighting = resolved[7]; }
    // All later post/editor/debug render targets are single sample.
    g_rhi->SelectMSAASamples(1);
    return true;
}

bool jRenderer::DecodeSkinPostNormals()
{
    if (!RenderFrameContextPtr->HasSkinMaterials) return true;
    auto& scene = *RenderFrameContextPtr->SceneRenderTargetPtr;
    auto info = scene.GBuffer[0]->Info;
    info.SampleCount = EMSAASamples::COUNT_1;
    info.ResourceName = jNameStatic("SkinGeometricNormalForPost");
    auto target = jRenderTargetPool::GetRenderTarget(info);
    if (!target) return false;
    auto* packed = scene.GBuffer[0]->GetTexture();
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), packed, EResourceLayout::SHADER_READ_ONLY);
    jRHIUtil::DrawFullScreen(RenderFrameContextPtr, target,
        [packed](const std::shared_ptr<jRenderFrameContext>&, jShaderBindingArray& array,
            jShaderBindingResourceInlineAllocator& allocator)
        {
            jSkinNormalDecodeParameters p; p.PackedNormal = {packed};
            jShaderParameterSet::BuildShaderBindings(p, EShaderAccessStageFlag::FRAGMENT, array, allocator);
        }, [](const std::shared_ptr<jRenderFrameContext>&)
        {
            jShaderInfo info; info.SetName(jNameStatic("SkinGeometricNormalDecodePS"));
            info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/skin_normal_decode_ps.hlsl"));
            info.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            jShaderParameterSet::AppendToShaderInfo<jSkinNormalDecodeParameters>(info, 0);
            return g_rhi->CreateShader(info);
        });
    scene.SkinIntermediateTargets.push_back(scene.GBuffer[0]);
    scene.GBuffer[0] = target;
    return true;
}

void jRenderer::SubsurfacePass()
{
    if (!RenderFrameContextPtr->UseSubsurface || !View.Camera) return;
    auto& scene = *RenderFrameContextPtr->SceneRenderTargetPtr;
    const auto copy = [&](const std::shared_ptr<jRenderTarget>& output)
    {
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), output->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), scene.ColorPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        auto* source = output->GetTexture();
        auto* mask = scene.SubsurfaceMask->GetTexture();
        auto* diffuse = scene.DiffuseLighting->GetTexture();
        jRHIUtil::DrawFullScreen(RenderFrameContextPtr, scene.ColorPtr,
            [source, mask, diffuse](const std::shared_ptr<jRenderFrameContext>&, jShaderBindingArray& bindings,
                jShaderBindingResourceInlineAllocator& allocator)
            {
                jSubsurfaceCopyParameters p;
                p.CopyColor = {source}; p.CopyMask = {mask}; p.CopyDiffuse = {diffuse};
                jShaderParameterSet::BuildShaderBindings(p, EShaderAccessStageFlag::FRAGMENT, bindings, allocator);
            }, [](const std::shared_ptr<jRenderFrameContext>&)
            {
                jShaderInfo info; info.SetName(jNameStatic("SubsurfaceCopyPS"));
                info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/subsurface_copy_ps.hlsl"));
                info.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                jShaderParameterSet::AppendToShaderInfo<jSubsurfaceCopyParameters>(info, 0);
                return g_rhi->CreateShader(info);
            });
    };
    jSubsurface::Inputs inputs;
    inputs.SceneColor = scene.ColorPtr; inputs.DiffuseLighting = scene.DiffuseLighting;
    inputs.Mask = scene.SubsurfaceMask; inputs.Depth = scene.DepthPtr; inputs.Normal = scene.GBuffer[0];
    inputs.InvVP = View.Camera->GetInverseViewProjectionMatrix();
    const auto result = jSubsurface::BuildAndRecord(RenderFrameContextPtr, inputs, gOptions.Subsurface);
    auto& diagnostic = MutableRendererFeatureDiagnostics().SubsurfaceSupportReason;
    diagnostic = result.Diagnostic;
    const auto recordTexture = [&]() -> std::shared_ptr<jRenderTarget>
    {
        if (!scene.HasTextureSubsurfaceMaterials || !scene.SkinUV) return {};
        if (!RenderFrameContextPtr->SkinEnergyLUT)
        { diagnostic += " Texture transport: skin energy LUT unavailable; screen fallback."; return {}; }

        // This first texture transport module isolates one analytic key. Preserve
        // the screen result when other diffuse writers cannot yet be separated.
        const auto& environment = gOptions.SceneFeatures.Environment;
        bool activeEnvironment = false;
        if (environment.Enabled && environment.Intensity > 0.f)
            for (const auto& coefficient : environment.Radiance)
                for (const auto value : coefficient) activeEnvironment |= value != 0.f;
        if (activeEnvironment || gOptions.UseSurfelGI || gOptions.UseSSGI)
        { diagnostic += " Texture transport: diffuse GI/environment uses screen fallback."; return {}; }
        const jViewLight* key = nullptr;
        for (const auto& candidate : View.Lights)
        {
            if (!candidate.Light) continue;
            Vector color(0.f);
            if (candidate.Light->Type == ELightType::DIRECTIONAL)
                color = static_cast<const jDirectionalLight*>(candidate.Light)->GetLightData().Color;
            else if (candidate.Light->Type == ELightType::POINT)
                color = static_cast<const jPointLight*>(candidate.Light)->GetLightData().Color;
            else
            {
                // Ignore disabled/black default spots; a contributing penumbra
                // needs its own isolated UV light writer.
                if (candidate.Light->Type == ELightType::SPOT)
                {
                    const auto spot = static_cast<const jSpotLight*>(candidate.Light)->GetLightData().Color;
                    if (!(spot.x > 0.f || spot.y > 0.f || spot.z > 0.f)) continue;
                }
                diagnostic += " Texture transport: spot-light scenes use screen fallback."; return {};
            }
            if (!(color.x > 0.f || color.y > 0.f || color.z > 0.f)) continue;
            if (key) { diagnostic += " Texture transport: multiple analytic keys use screen fallback."; return {}; }
            key = &candidate;
        }
        if (!key) { diagnostic += " Texture transport: no analytic key; screen fallback."; return {}; }
        if (!key->Light->IsShadowCaster)
        { diagnostic += " Texture transport: the key's shadow capture is disabled; screen fallback."; return {}; }
        jTextureSubsurface::Inputs transport;
        transport.SkinDraws = BasePasses;
        transport.CaptureDraws = BasePasses;
        transport.SceneAfterScreen = result.Applied ? result.Output : scene.ColorPtr;
        transport.ScreenFilteredDiffuse = result.Applied ? result.FilteredDiffuse : nullptr;
        transport.OriginalDiffuse = scene.DiffuseLighting;
        transport.Albedo = scene.GBuffer[1]; transport.Mask = scene.SubsurfaceMask; transport.SkinUV = scene.SkinUV;
        transport.Strength = gOptions.Subsurface.Strength;
        transport.Key.EnergyLUT = RenderFrameContextPtr->SkinEnergyLUT;
        if (key->Light->Type == ELightType::POINT)
        {
            const auto& data = static_cast<const jPointLight*>(key->Light)->GetLightData();
            transport.Key.PositionAndReferenceDistance = Vector4(data.Position, 1.f);
            transport.Key.ColorAndType = Vector4(data.Color, 1.f);
            transport.Key.MaxDistance = data.MaxDistance;
        }
        else
        {
            const auto& data = static_cast<const jDirectionalLight*>(key->Light)->GetLightData();
            transport.Key.ColorAndType = Vector4(data.Color, 0.f);
            transport.Key.DirectionAndFar = Vector4(data.Direction, data.Far);
        }
        const auto uv = jTextureSubsurface::BuildAndRecord(RenderFrameContextPtr, View, transport, gOptions.TextureSubsurface);
        diagnostic += " " + uv.Diagnostic;
        return uv.Applied ? uv.Output : nullptr;
    };
    // Compose both diffusion spaces before the single final HDR format write.
    const auto output = recordTexture();
    if (output) copy(output);
    else if (result.Applied) copy(result.Output);
}
