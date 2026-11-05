#include "pch.h"
#include "jRenderer.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "Material/jMaterial.h"
#include "Shader/jCommonShaderParameters.h"
#include "Shader/jShader.h"
#include "RHI/jRHIUtil.h"
#include "RHI/jRenderTargetPool.h"
#include "jOptions.h"
#include <cstdlib>

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorUiRenderer.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#endif

namespace
{
struct jShaderHitObjectVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters,
        jMaterialShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderHitObjectVertexShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHitObjectVertexShader
    , "HitObject_vs"
    , "Resource/Shaders/hlsl/HitObject_vs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::VERTEX)

struct jShaderHitObjectPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters,
        jMaterialShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderHitObjectPixelShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHitObjectPixelShader
    , "HitObject_ps"
    , "Resource/Shaders/hlsl/HitObject_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)
#ifdef ENABLE_EDITOR_FEATURES
BEGIN_SHADER_PARAMETER_SET(jEditorSelectionOutlineParameters)
    SHADER_TEXTURE2D_SRV(SelectionMask)
END_SHADER_PARAMETER_SET()

struct jEditorSelectionOutlinePixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jEditorSelectionOutlineParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jEditorSelectionOutlinePixelShader, Permutation)
};
IMPLEMENT_SHADER_WITH_PERMUTATION(jEditorSelectionOutlinePixelShader
    , "EditorSelectionOutline_ps"
    , "Resource/Shaders/hlsl/EditorSelectionOutline_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)

std::shared_ptr<jRenderTarget> ObjectIdTarget(int32 width, int32 height, const jName& name)
{
    return jRenderTargetPool::GetRenderTargetForOneFrame({
        .Format = ETextureFormat::RGBA8,
        .Width = width,
        .Height = height,
        .RTClearValue = jRTClearValue(0.f, 0.f, 0.f, 0.f),
        .TextureCreateFlag = ETextureCreateFlag::RTV | ETextureCreateFlag::TransferSrc,
        .ResourceName = name
    });
}

bool DrawObjectIds(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view,
    const std::shared_ptr<jRenderTarget>& target, std::span<jRenderObject* const> objects,
    const std::shared_ptr<jRenderTarget>& depth = {})
{
    if (!target || !target->GetTexture() || !view.ViewUniformBufferShaderBindingInstance) return false;
    auto* command = frame->GetActiveCommandBuffer();
    const int32 width = target->Info.Width, height = target->Info.Height;
    const auto colorLayout = EResourceLayout::COLOR_ATTACHMENT;
    g_rhi->TransitionLayout(command, target->GetTexture(), colorLayout);
    jRenderPassInfo pass;
    pass.Attachments.push_back({
        .RenderTargetPtr = target,
        .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
        .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
        .RTClearValue = target->Info.RTClearValue,
        .InitialLayout = colorLayout,
        .FinalLayout = colorLayout
    });
    jSubpass subpass;
    subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
    subpass.OutputColorAttachments.push_back(0);
    if (depth)
    {
        const auto depthLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT;
        g_rhi->TransitionLayout(command, depth->GetTexture(), depthLayout);
        subpass.OutputDepthAttachment = 1;
        pass.Attachments.push_back({
            .RenderTargetPtr = depth,
            .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
            .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
            .RTClearValue = depth->Info.RTClearValue,
            .InitialLayout = depthLayout,
            .FinalLayout = depthLayout
        });
    }
    pass.Subpasses.push_back(subpass);
    auto* renderPass = g_rhi->GetOrCreateRenderPass(pass, {0, 0}, {width, height});
    if (!renderPass) return false;

    auto* raster = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE, EFrontFace::CCW, false,
        0.f, 0.f, 0.f, 1.f, false, false, EMSAASamples::COUNT_1, false, 0.f, false, false>::Create();
    auto* depthState = depth
        ? TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.f, 1.f>::Create()
        : TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.f, 1.f>::Create();
    auto* blend = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD,
        EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    jPipelineStateFixedInfo pipeline(raster, depthState, blend, jViewport(0.f, 0.f, float(width), float(height)),
        jScissor(0, 0, width, height), false);
    jGraphicsPipelineShader shader;
    shader.VertexShader = jShaderHitObjectVertexShader::CreateShader({});
    shader.PixelShader = jShaderHitObjectPixelShader::CreateShader({});
    if (!shader.VertexShader || !shader.PixelShader) return false;

    // Keep a fixed view, object, and material layout across both raster pipelines.
    std::vector<jDrawCommand> draws;
    draws.reserve(objects.size());
    for (auto* object : objects)
    {
        if (!object || !object->GeometryDataPtr || !object->GeometryDataPtr->VertexBufferPtr) continue;
        auto* material = object->MaterialPtr ? object->MaterialPtr.get() : GDefaultMaterial.get();
        if (!material) continue;
        jShaderBindingInstanceGroup bindings;
        bindings.Add(view.ViewUniformBufferShaderBindingInstance);
        bindings.Add(object->CreateShaderBindingInstance());
        bindings.Add(material->CreateShaderBindingInstance());
        draws.emplace_back(frame, &view, object, renderPass, shader, &pipeline, material, bindings, nullptr,
            nullptr, 0, EDrawCommandBindingMode::Manual);
        draws.back().PrepareToDraw(false);
    }
    command->FlushBarrierBatch();
    if (!renderPass->BeginRenderPass(command)) return false;
    for (const auto& draw : draws) draw.Draw();
    renderPass->EndRenderPass();
    return true;
}
#endif
}

void IRenderer::RequestObjectPick(int32 mouseX, int32 mouseY)
{
    bObjectPickRequested = true;
    PickMouseX = mouseX;
    PickMouseY = mouseY;
}

void IRenderer::HitObjectPass()
{
#ifdef ENABLE_EDITOR_FEATURES
    if (!g_Editor || !g_Editor->Placement.EnablePlacementMode || IsAuthoringRuntimeMode())
    {
        bObjectPickRequested = false;
        if (g_Editor) g_Editor->Placement.bPickRequested = false;
        return;
    }
    auto& placement = g_Editor->Placement;
    const bool authoring = IsAuthoringWorkspaceVisible() && !g_Editor->GetDocument().IsPlaying();
    bool toggleSelection = false;
    if (placement.bPickRequested)
    {
        RequestObjectPick(placement.PickMouseX, placement.PickMouseY);
        toggleSelection = authoring && placement.PickToggleSelection;
        placement.bPickRequested = false;
        placement.PickToggleSelection = false;
    }
    if (!bObjectPickRequested) return;
    bObjectPickRequested = false;

    SCOPE_CPU_PROFILE(HitObjectPass);
    auto& target = jSceneRenderTarget::HitObject_RT;
    if (!target || target->Info.Width != SCR_WIDTH || target->Info.Height != SCR_HEIGHT)
        target = g_rhi->CreateRenderTarget({
            .Format = ETextureFormat::RGBA8,
            .Width = SCR_WIDTH,
            .Height = SCR_HEIGHT,
            .RTClearValue = jRTClearValue(0.f, 0.f, 0.f, 0.f),
            .TextureCreateFlag = ETextureCreateFlag::RTV | ETextureCreateFlag::TransferSrc,
            .ResourceName = jNameStatic("HitObject_RT")
        });
    auto depth = jRenderTargetPool::GetRenderTargetForOneFrame({
        .Format = ETextureFormat::D32,
        .Width = SCR_WIDTH,
        .Height = SCR_HEIGHT,
        .RTClearValue = jRTClearValue(1.f, 0),
        .ResourceName = jNameStatic("HitObjectDepth_RT")
    });
    if (!depth || !depth->GetTexture()) return;
    auto objects = jObject::GetStaticRenderObject();
    if (gOptions.ShowDebugObject)
        for (const auto* debug : jObject::GetDebugObject())
            if (debug->Visible) objects.insert(objects.end(), debug->RenderObjects.begin(), debug->RenderObjects.end());
    bool drawn;
    {
        DEBUG_EVENT(RenderFrameContextPtr, "HitObjectPass");
        drawn = DrawObjectIds(RenderFrameContextPtr, View, target, objects, depth);
    }
    if (drawn) ReadbackHitObject(target, authoring, toggleSelection);
#endif
}

void IRenderer::ReadbackHitObject(const std::shared_ptr<jRenderTarget>& target, bool authoring, bool toggleSelection)
{
#ifdef ENABLE_EDITOR_FEATURES
    // DX12 requires a 256-byte aligned row pitch even for a 1x1 region.
    static constexpr uint32 rowPitch = 256;
    static constexpr uint64 placementAlignment = 512;
    // The synchronous copy lets the buffer lease end before device shutdown.
    const auto readback = g_rhi->CreateRawBuffer(rowPitch, placementAlignment, EBufferCreateFlag::Readback,
        EResourceLayout::TRANSFER_DST, nullptr, rowPitch, jNameStatic("HitObject_ReadbackBuffer"));
    if (!readback) return;

    auto* command = RenderFrameContextPtr->GetActiveCommandBuffer();
    g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::TRANSFER_SRC);
    command->FlushBarrierBatch();
    jTextureCopyRegion region;
    region.X = Clamp(PickMouseX, 0, target->Info.Width - 1);
    region.Y = Clamp(PickMouseY, 0, target->Info.Height - 1);
    region.Width = region.Height = 1;
    region.DestRowPitchOverride = rowPitch;
    g_rhi->CopyTextureRegionToBuffer(command, target->GetTexture(), region, readback.get(), readback->GetOffset());
    if (IsUseVulkan())
    {
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier((VkCommandBuffer)command->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    }
    RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, true);
    RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();

    const auto* bytes = static_cast<const uint8*>(readback->Map());
    if (!bytes) return;
    const jRenderObjectID objectId = jRenderObjectID(bytes[0]) | (jRenderObjectID(bytes[1]) << 8) |
        (jRenderObjectID(bytes[2]) << 16) | (jRenderObjectID(bytes[3]) << 24);
    readback->Unmap();
#ifdef _WIN32
    char trace[2]{};
    if (GetEnvironmentVariableA("JENGINE_TEST_SCENE_FEATURES", trace, sizeof(trace)) == 1 && trace[0] == '1')
#else
    if (const auto* trace = std::getenv("JENGINE_TEST_SCENE_FEATURES"); trace && trace[0] == '1')
#endif
        std::cout << "SCENE_FEATURE_PICK frame=" << g_rhi->GetCurrentFrameNumber()
            << " forward=" << RenderFrameContextPtr->UseForwardRenderer
            << " x=" << PickMouseX << " y=" << PickMouseY
            << " readback_offset=" << readback->GetOffset()
            << " render_object_id=" << objectId << std::endl;
    auto* picked = objectId ? jRenderObject::FindRenderObjectByID(objectId) : nullptr;
    if (authoring)
    {
        g_Editor->SelectViewportHit(picked, toggleSelection);
        RevealAuthoringSelection(*g_Editor);
    }
    else g_Editor->Placement.SelectObject(picked);
#endif
}

#ifdef ENABLE_EDITOR_FEATURES
void jEditorUiRenderer::DrawSelectionOutline(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view,
    const std::shared_ptr<jRenderTarget>& preview)
{
    if (!g_Editor) return;
    const auto meshes = g_Editor->GetSelectionRenderObjects();
    if (meshes.empty()) return;
    auto* pixelShader = jEditorSelectionOutlinePixelShader::CreateShader({});
    if (!pixelShader) return;
    DEBUG_EVENT(frame, "Editor selection outline");
    const auto mask = ObjectIdTarget(preview->Info.Width, preview->Info.Height, jNameStatic("EditorSelectionMask_RT"));
    if (!DrawObjectIds(frame, view, mask, meshes)) return;
    g_rhi->TransitionLayout(frame->GetActiveCommandBuffer(), mask->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    jRHIUtil::DrawFullScreen(frame, preview,
        [&](const auto&, jShaderBindingArray& bindings, jShaderBindingResourceInlineAllocator& allocator) {
            jEditorSelectionOutlineParameters parameters;
            parameters.SelectionMask.Texture = mask->GetTexture();
            jShaderParameterSet::BuildShaderBindings(parameters, EShaderAccessStageFlag::FRAGMENT, bindings, allocator);
        },
        [pixelShader](const auto&) { return pixelShader; },
        [](jRasterizationStateInfo*& raster, jBlendingStateInfo*& blend, jDepthStencilStateInfo*& depth) {
            raster = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE, EFrontFace::CCW, false,
                0.f, 0.f, 0.f, 1.f, false, false, EMSAASamples::COUNT_1, false, 0.f, false, false>::Create();
            blend = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD,
                EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
            depth = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.f, 1.f>::Create();
        });
}
#endif
