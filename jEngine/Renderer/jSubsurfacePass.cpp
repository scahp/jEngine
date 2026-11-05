#include "pch.h"
#include "jSubsurfacePass.h"
#include "Features/jSubsurfaceDiffusion.h"
#include "RHI/jRenderTargetPool.h"
#include "Shader/jSubsurfaceShaderParameters.h"
#include "Profiler/jPerformanceProfile.h"

namespace jSubsurface
{
namespace
{
Inputs LastInputs;
Result LastResult;
std::uint64_t LastFrame = ~std::uint64_t(0);

template<typename T>
std::shared_ptr<IUniformBufferBlock> Uniform(const char* name, const T& values)
{
    auto buffer = g_rhi->CreateUniformBufferBlock(jName(name), jLifeTimeType::OneFrame, sizeof(T));
    if (buffer) buffer->UpdateBufferData(&values, sizeof(T));
    return buffer;
}

bool ValidateInputs(const Inputs& inputs, std::uint32_t& width, std::uint32_t& height,
    std::size_t& bytes, std::string& error)
{
    if (!inputs.SceneColor || inputs.SceneColor->Info.Width <= 0 || inputs.SceneColor->Info.Height <= 0)
    {
        error = "Subsurface diffusion requires a valid stored scene colour.";
        return false;
    }
    width = std::uint32_t(inputs.SceneColor->Info.Width);
    height = std::uint32_t(inputs.SceneColor->Info.Height);
    if (!ValidateExtent(width, height, bytes, error)) return false;
    for (const auto& target : {inputs.SceneColor, inputs.DiffuseLighting, inputs.Mask, inputs.Depth, inputs.Normal})
    {
        if (!target || !target->GetTexture() || !target->GetTexture()->GetHandle() ||
            target->Info.Type != ETextureType::TEXTURE_2D || target->Info.IsMemoryless ||
            target->Info.SampleCount != EMSAASamples::COUNT_1 ||
            target->Info.Width != int32(width) || target->Info.Height != int32(height))
        {
            error = "Subsurface diffusion requires equally sized stored single-sample colour, diffuse, mask, depth and normal inputs.";
            return false;
        }
    }
    if ((inputs.DiffuseLighting->Info.Format != ETextureFormat::RGBA16F &&
         inputs.DiffuseLighting->Info.Format != ETextureFormat::RGBA32F) ||
        (inputs.Mask->Info.Format != ETextureFormat::RGBA16F &&
         inputs.Mask->Info.Format != ETextureFormat::RGBA32F))
    {
        error = "Subsurface diffuse/thickness and mask/profile inputs require RGBA16F or RGBA32F storage.";
        return false;
    }
    if (!std::isfinite(inputs.DepthClearValue) ||
        (inputs.DepthClearValue != 0.0f && inputs.DepthClearValue != 1.0f))
    {
        error = "Subsurface diffusion requires the actual zero or one depth clear value.";
        return false;
    }
    for (float component : inputs.InvVP.mm)
    {
        if (!std::isfinite(component))
        {
            error = "Subsurface diffusion requires a finite inverse view-projection matrix.";
            return false;
        }
    }
    return true;
}

struct PreparedPass
{
    jPipelineStateInfo* Pipeline = nullptr;
    jShaderBindingInstanceGroup Bindings;
};

bool PreparePass(PreparedPass& output, std::uint32_t pass,
    const jSubsurfaceComputeParameters& parameters, std::string& error)
{
    static constexpr const char* names[] = {
        "SubsurfaceHorizontalCS", "SubsurfaceVerticalCS", "SubsurfaceCompositionCS"};
    jShaderInfo info;
    info.SetName(jName(names[pass]));
    // The runtime resolves every nested include from the entry's parent.
    info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/subsurface_cs.hlsl"));
    info.SetEntryPoint(jNameStatic("main"));
    info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    info.SetPreProcessors(jName("#define JE_SUBSURFACE_PASS " + std::to_string(pass) + "\n"));
    jShaderParameterSet::AppendToShaderInfo<jSubsurfaceComputeParameters>(info, 0);
    const auto* shader = g_rhi->CreateShader(info);
    if (!shader || !shader->GetCompiledShader())
    {
        error = std::string("Subsurface shader creation failed: ") + names[pass];
        return false;
    }
    auto binding = jShaderParameterSet::CreateShaderBindingInstance(parameters,
        EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);
    if (!binding)
    {
        error = std::string("Subsurface resource binding failed: ") + names[pass];
        return false;
    }
    output.Bindings.Add(binding);
    output.Pipeline = g_rhi->CreateComputePipelineStateInfo(shader, output.Bindings.GetLayoutArray(), {});
    if (!output.Pipeline)
    {
        error = std::string("Subsurface compute pipeline creation failed: ") + names[pass];
        return false;
    }
    return true;
}
}

Result BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame,
    const Inputs& inputs, const Settings& inputSettings)
{
    Result result;
    result.Output = inputs.SceneColor;
    const auto publish = [&]()
    {
        LastInputs = inputs;
        LastResult = result;
        LastFrame = g_rhi ? g_rhi->GetCurrentFrameNumber() : ~std::uint64_t(0);
        return result;
    };
    Settings settings;
    if (!ValidateSettings(inputSettings, settings, result.Diagnostic)) return publish();
    if (!HasEffect(settings)) return publish();
    if (!frame || !g_rhi || !frame->GetActiveCommandBuffer())
    {
        result.Diagnostic = "Subsurface diffusion requires an active owner-thread render command buffer.";
        return publish();
    }
    std::uint32_t width = 0, height = 0;
    std::size_t scratchBytes = 0;
    if (!ValidateInputs(inputs, width, height, scratchBytes, result.Diagnostic)) return publish();

    jSubsurfaceViewData view{};
    view.InvVP = inputs.InvVP;
    view.SizeAndInvSize = Vector4(float(width), float(height), 1.0f / width, 1.0f / height);
    view.FilterParams = Vector4(settings.Strength, settings.DepthRejectWorld,
        settings.NormalThreshold, settings.MaxRadiusPixels);
    view.PassParams = Vector4(settings.RadiusScale, inputs.DepthClearValue, 0.0f, 0.0f);
    jSubsurfaceProfileData profiles{};
    for (std::uint32_t profile = 0; profile < MaximumProfiles; ++profile)
    {
        Kernel kernel;
        if (!BuildKernel(Profiles[profile], kernel, result.Diagnostic)) return publish();
        const auto& preset = Profiles[profile];
        profiles.RadiusWorld[profile] = Vector4(preset.RadiusWorld * preset.Falloff[0],
            preset.RadiusWorld * preset.Falloff[1], preset.RadiusWorld * preset.Falloff[2], preset.RadiusWorld);
        for (std::uint32_t tap = 0; tap < KernelSampleCount; ++tap)
            profiles.Kernel[profile * KernelSampleCount + tap] = Vector4(kernel[tap].Weight[0],
                kernel[tap].Weight[1], kernel[tap].Weight[2], kernel[tap].Offset);
    }
    const auto viewBuffer = Uniform("SubsurfaceView", view);
    const auto profileBuffer = Uniform("SubsurfaceProfiles", profiles);
    if (!viewBuffer || !profileBuffer)
    {
        result.Diagnostic = "Subsurface uniform allocation failed; original lighting retained.";
        return publish();
    }

    const jRenderTargetInfo scratch = {
        .Type = ETextureType::TEXTURE_2D, .Format = ETextureFormat::RGBA16F,
        .Width = int32(width), .Height = int32(height), .SampleCount = EMSAASamples::COUNT_1,
        .TextureCreateFlag = ETextureCreateFlag::UAV | ETextureCreateFlag::TransferSrc,
        .ResourceName = jNameStatic("SubsurfaceScratch")};
    std::array<std::shared_ptr<jRenderTarget>, 3> targets;
    for (auto& target : targets)
    {
        target = jRenderTargetPool::GetRenderTargetForOneFrame(scratch);
        if (!target || !target->GetTexture() || !target->GetTexture()->GetHandle())
        {
            result.Diagnostic = "Subsurface scratch allocation failed; original lighting retained.";
            return publish();
        }
        result.AdditionalTargetBytes += std::size_t(width) * height * 8u;
    }

    std::array<PreparedPass, 3> passes;
    for (std::uint32_t pass = 0; pass < passes.size(); ++pass)
    {
        jSubsurfaceComputeParameters parameters;
        parameters.SSSView.Buffer = viewBuffer;
        parameters.SSSProfiles.Buffer = profileBuffer;
        parameters.SSSSceneColor = {inputs.SceneColor->GetTexture()};
        parameters.SSSOriginalDiffuse = {inputs.DiffuseLighting->GetTexture()};
        parameters.SSSInputDiffuse = {pass == 0 ? inputs.DiffuseLighting->GetTexture() : targets[pass - 1]->GetTexture()};
        parameters.SSSMask = {inputs.Mask->GetTexture()};
        parameters.SSSDepth = {inputs.Depth->GetTexture()};
        parameters.SSSNormal = {inputs.Normal->GetTexture()};
        parameters.SSSResult = {targets[pass]->GetTexture()};
        if (!PreparePass(passes[pass], pass, parameters, result.Diagnostic)) return publish();
    }

    SCOPE_GPU_PROFILE(frame, SubsurfaceDiffusion);
    DEBUG_EVENT_WITH_COLOR(frame, "SubsurfaceDiffusion", Vector4(0.8f, 0.35f, 0.3f, 1.0f));
    auto* command = frame->GetActiveCommandBuffer();
    for (const auto& target : {inputs.SceneColor, inputs.DiffuseLighting, inputs.Mask, inputs.Depth, inputs.Normal})
        g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    for (std::uint32_t pass = 0; pass < passes.size(); ++pass)
    {
        // Distinct source/destination images and explicit write-to-read barriers
        // avoid typed-UAV read assumptions and any in-place convolution hazard.
        g_rhi->TransitionLayout(command, targets[pass]->GetTexture(), EResourceLayout::GENERAL);
        passes[pass].Pipeline->Bind(frame);
        g_rhi->BindComputeShaderBindingInstances(command, passes[pass].Pipeline,
            passes[pass].Bindings.GetCombiner(), 0);
        g_rhi->DispatchCompute(frame, (width + 7u) / 8u, (height + 7u) / 8u, 1);
        ++result.ComputePasses;
        g_rhi->TransitionLayout(command, targets[pass]->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    }
    result.Output = targets[2];
    result.FilteredDiffuse = targets[1];
    result.Applied = true;
    return publish();
}

bool GetLastDebugOutput(std::uint64_t& frameNumber, Inputs& inputs, Result& result)
{
    if (!g_rhi || LastFrame != g_rhi->GetCurrentFrameNumber()) return false;
    frameNumber = LastFrame;
    inputs = LastInputs;
    result = LastResult;
    return true;
}

void ReleaseResources()
{
    LastInputs = {};
    LastResult = {};
    LastFrame = ~std::uint64_t(0);
}
}
