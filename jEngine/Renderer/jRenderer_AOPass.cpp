#include "pch.h"
#include "jRenderer.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "Material/jMaterial.h"
#include "RHI/jShaderBindingLayout.h"
#include "RHI/jTexture.h"
#include "Scene/jRenderObject.h"
#include "Profiler/jPerformanceProfile.h"
#include "Scene/Light/jDirectionalLight.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRaytracingScene.h"
#include "RHI/jRHIUtil.h"

static float RTScale = 0.0f;
static int32 RayRTWidth = 0;
static int32 RayRTHeight = 0;

std::shared_ptr<jTexture> ReprojectionAO(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const std::shared_ptr<jTexture>& InTexture)
{
    if (gOptions.UseAOReprojection)
    {
        struct CommonComputeUniformBuffer
        {
            int32 Width;
            int32 Height;
            int32 FrameNumber;
            float InvScaleToOriginBuffer;           // InvScale for HistoryBuffer, it will be always 1.0 when no scaling options(RTScale == 1).
        };
        CommonComputeUniformBuffer CommonComputeData;
        CommonComputeData.Width = InTexture->Width;
        CommonComputeData.Height = InTexture->Height;
        CommonComputeData.FrameNumber = g_rhi->GetCurrentFrameNumber();
        CommonComputeData.InvScaleToOriginBuffer = 1.0f / RTScale;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("ReprojectionAOUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        // Temporal Previous Frame AO Reprojection
        {
            DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Reprojection AO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
            SCOPE_CPU_PROFILE(ReprojectionAO);
            SCOPE_GPU_PROFILE(InRenderFrameContextPtr, ReprojectionAO);

            jRHIUtil::DrawFullScreen(InRenderFrameContextPtr, jSceneRenderTarget::AOProjection
                , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::HistoryBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[3]->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::HistoryDepthBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture.get(), SamplerState)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(jSceneRenderTarget::HistoryBuffer.get(), SamplerState)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::FRAGMENT
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[3]->GetTexture(), nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::FRAGMENT
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::FRAGMENT
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(jSceneRenderTarget::HistoryDepthBuffer.get(), nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::FRAGMENT
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
                }
                , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                    {
                        jShaderInfo shaderInfo;
                        shaderInfo.SetName(jNameStatic("ReProjectionAOPS"));
                        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AOReprojection_cs.hlsl"));
                        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                        shaderInfo.SetEntryPoint(jNameStatic("AOReprojectionPS"));
                        if (gOptions.UseDiscontinuityWeight)
                        {
                            shaderInfo.SetPreProcessors(jNameStatic("#define USE_DISCONTINUITY_WEIGHT 1"));
                        }
                        return g_rhi->CreateShader(shaderInfo);
                    });
        }
        {
            DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "CopyDepthBuffer", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
            SCOPE_CPU_PROFILE(CopyDepthBuffer);
            SCOPE_GPU_PROFILE(InRenderFrameContextPtr, CopyDepthBuffer);

            struct CommonComputeUniformBuffer
            {
                int32 Width;
                int32 Height;
                int32 Paading0;
                float Padding1;
            };
            CommonComputeUniformBuffer CommonComputeData;
            CommonComputeData.Width = jSceneRenderTarget::HistoryDepthBuffer->Width;
            CommonComputeData.Height = jSceneRenderTarget::HistoryDepthBuffer->Height;

            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                jNameStatic("CopyCSOneFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
            OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

            jRHIUtil::DispatchCompute(InRenderFrameContextPtr, jSceneRenderTarget::HistoryDepthBuffer.get()
                , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    jTexture* InTexture = InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture();
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture, nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
                }
                , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                    {
                        jShaderInfo shaderInfo;
                        shaderInfo.SetName(jNameStatic("CopyCS"));
                        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_cs.hlsl"));
                        shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                        jShader* Shader = g_rhi->CreateShader(shaderInfo);
                        return Shader;
                    }
                );
        }
        return jSceneRenderTarget::AOProjection->GetTexturePtr();
    }

    return InTexture;
}

std::shared_ptr<jTexture> DenoisingAO(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const std::shared_ptr<jTexture>& InTexture)
{
    if (gOptions.IsDenoiserGuassianSeparable())
    {
        DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "GaussianSeparable", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(GaussianSeparable);
        SCOPE_GPU_PROFILE(InRenderFrameContextPtr, GaussianSeparable);

        auto createGaussianKernel = [](int32 kernelSize, float sigma) -> std::vector<float>
            {
                std::vector<float> kernel(kernelSize);
                int32 center = kernelSize / 2;
                float sum = 0.0;

                for (int32 i = 0; i < kernelSize; ++i)
                {
                    float x = (float)(i - center);
                    kernel[i] = exp(-(x * x) / (2 * sigma * sigma)) / (sqrt(2 * PI) * sigma);
                    sum += kernel[i];
                }

                // Normalize the kernel
                for (int32 i = 0; i < kernelSize; ++i)
                {
                    kernel[i] /= sum;
                }

                return kernel;
            };

        std::vector<float> GaussianKernel = createGaussianKernel(gOptions.GaussianKernelSize, gOptions.GaussianKernelSigma);

        // Create GaussianBlurKernel uniformbuffer
        struct jGaussianBlurKernel
        {
            Vector4 Width[20];
        };
        jGaussianBlurKernel KernelData;
        check(sizeof(KernelData.Width) >= GaussianKernel.size() * sizeof(float));
        memcpy(KernelData.Width, GaussianKernel.data(), GaussianKernel.size() * sizeof(float));

        auto OneFrameGaussianKernelUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("GaussianKernel"), jLifeTimeType::OneFrame, sizeof(KernelData)));
        OneFrameGaussianKernelUniformBuffer->UpdateBufferData(&KernelData, sizeof(KernelData));

        // Create common uniformbuffer
        struct CommonComputeUniformBuffer
        {
            int32 Width;
            int32 Height;
            int32 KernelSize;
            int32 Padding0;
        };
        CommonComputeUniformBuffer CommonComputeData;
        CommonComputeData.Width = RayRTWidth;
        CommonComputeData.Height = RayRTHeight;
        CommonComputeData.KernelSize = (int32)GaussianKernel.size();

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        g_rhi->UAVBarrier(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get());
        {
            DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Vertical", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
            SCOPE_CPU_PROFILE(Vertical);
            SCOPE_GPU_PROFILE(InRenderFrameContextPtr, Vertical);

            jRHIUtil::DispatchCompute(InRenderFrameContextPtr, jSceneRenderTarget::GaussianV.get()
                , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture.get(), nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameGaussianKernelUniformBuffer.get()), true));
                }
                , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                    {
                        jShaderInfo shaderInfo;
                        shaderInfo.SetName(jNameStatic("GaussianV"));
                        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/gaussianblur_cs.hlsl"));
                        shaderInfo.SetEntryPoint(jNameStatic("Vertical"));
                        shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                        jShader* Shader = g_rhi->CreateShader(shaderInfo);
                        return Shader;
                    }
                );
        }

        g_rhi->UAVBarrier(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::GaussianV.get());

        {
            DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Horizon", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
            SCOPE_CPU_PROFILE(Horizon);
            SCOPE_GPU_PROFILE(InRenderFrameContextPtr, Horizon);

            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
            OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

            jRHIUtil::DispatchCompute(InRenderFrameContextPtr, jSceneRenderTarget::GaussianH.get()
                , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    jTexture* InGaussianVTexture = jSceneRenderTarget::GaussianV.get();
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InGaussianVTexture, EResourceLayout::SHADER_READ_ONLY);

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InGaussianVTexture, nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameGaussianKernelUniformBuffer.get()), true));
                }
                , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                    {
                        jShaderInfo shaderInfo;
                        shaderInfo.SetName(jNameStatic("GaussianH"));
                        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/gaussianblur_cs.hlsl"));
                        shaderInfo.SetEntryPoint(jNameStatic("Horizon"));
                        shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                        jShader* Shader = g_rhi->CreateShader(shaderInfo);
                        return Shader;
                    }
                );
        }
        return jSceneRenderTarget::GaussianH;
    }
    else if (gOptions.IsDenoiserGuassian() || gOptions.IsDenoiserBilateral())
    {
        DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, gOptions.IsDenoiserBilateral() ? "Bilateral" : "Gaussian", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

        auto createGaussian2DKernel = [](int32 kernelSize, float sigma) -> std::vector<float>
            {
                std::vector<float> kernel(kernelSize * kernelSize);
                int32 center = kernelSize / 2;
                float sum = 0.0;

                int32 Index = 0;
                for (int32 j = 0; j < kernelSize; ++j)
                {
                    for (int32 i = 0; i < kernelSize; ++i)
                    {
                        float x = (float)(i - center);
                        float y = (float)(j - center);
                        kernel[Index] = exp(-0.5f * (x * x + y * y) / (sigma * sigma)) / (2 * PI * sigma * sigma);
                        sum += kernel[Index];
                        ++Index;
                    }
                }

                // Normalize the kernel
                for (int32 i = 0; i < (int32)kernel.size(); ++i)
                {
                    kernel[i] /= sum;
                }

                return kernel;
            };

        std::vector<float> GaussianKernel = createGaussian2DKernel(gOptions.GaussianKernelSize, gOptions.GaussianKernelSigma);

        jName ProfileTitle = gOptions.IsDenoiserBilateral() ? jNameStatic("Bilateral") : jNameStatic("Gaussian");
        SCOPE_CPU_PROFILE(ProfileTitle);
        SCOPE_GPU_PROFILE_NAME(InRenderFrameContextPtr, ProfileTitle);
        g_rhi->UAVBarrier(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get());
        {
            struct CommonComputeUniformBuffer
            {
                int32 Width;
                int32 Height;
                float Sigma;
                int KernelSize;
                float SigmaForBilateral;
                Vector Padding0;
            };
            CommonComputeUniformBuffer CommonComputeData;
            CommonComputeData.Width = jSceneRenderTarget::GaussianH->Width;
            CommonComputeData.Height = jSceneRenderTarget::GaussianH->Height;
            CommonComputeData.Sigma = gOptions.GaussianKernelSigma;
            CommonComputeData.KernelSize = gOptions.GaussianKernelSize;
            CommonComputeData.SigmaForBilateral = gOptions.BilateralKernelSigma;

            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
            OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

            // Create GaussianBlurKernel uniformbuffer
            struct jGaussianBlurKernel
            {
                Vector4 Width[150];
            };
            jGaussianBlurKernel KernelData;
            check(sizeof(KernelData.Width) >= GaussianKernel.size() * sizeof(float));
            memcpy(KernelData.Width, GaussianKernel.data(), GaussianKernel.size() * sizeof(float));

            auto OneFrameGaussianKernelUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                jNameStatic("GaussianKernel"), jLifeTimeType::OneFrame, sizeof(KernelData)));
            OneFrameGaussianKernelUniformBuffer->UpdateBufferData(&KernelData, sizeof(KernelData));

            jRHIUtil::DispatchCompute(InRenderFrameContextPtr, jSceneRenderTarget::GaussianH.get()
                , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                        , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
                        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture.get(), nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameGaussianKernelUniformBuffer.get()), true));
                }
                , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                    {
                        jShaderBilateralComputeShader::ShaderPermutation ShaderPermutation;
                        ShaderPermutation.SetIndex<jShaderBilateralComputeShader::USE_GAUSSIAN_INSTEAD>(gOptions.IsDenoiserGuassian());
                        jShader* Shader = jShaderBilateralComputeShader::CreateShader(ShaderPermutation);
                        return Shader;
                    }
                );
        }

        g_rhi->UAVBarrier(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::GaussianH.get());
        return jSceneRenderTarget::GaussianH;
    }

    return InTexture;
}

std::shared_ptr<jTexture> UpdateHistoryBuffer(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const std::shared_ptr<jTexture>& InTexture)
{
    // Copy HistoryBuffer
    DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Copy HistoryBuffer", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
    SCOPE_CPU_PROFILE(CopyHistoryBuffer);
    SCOPE_GPU_PROFILE(InRenderFrameContextPtr, CopyHistoryBuffer);

    struct CommonComputeUniformBuffer
    {
        int32 Width;
        int32 Height;
        int32 Paading0;
        float Padding1;
    };
    CommonComputeUniformBuffer CommonComputeData;
    CommonComputeData.Width = jSceneRenderTarget::HistoryBuffer->Width;
    CommonComputeData.Height = jSceneRenderTarget::HistoryBuffer->Height;

    auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("CopyCSOneFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
    OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

    jRHIUtil::DispatchCompute(InRenderFrameContextPtr, jSceneRenderTarget::HistoryBuffer.get()
        , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
        {
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);

            InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE
                , InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture.get(), nullptr)));

            InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
        }
        , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
            {
                jShaderInfo shaderInfo;
                shaderInfo.SetName(jNameStatic("CopyCS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_cs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                jShader* Shader = g_rhi->CreateShader(shaderInfo);
                return Shader;
            }
        );

    return InTexture;
}

void jRenderer::AOPass()
{
    if (!gOptions.UseRTAO)
        return;

    if (!GSupportRaytracing)
        return;

    SCOPE_CPU_PROFILE(RaytracingAO);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, RaytracingAO);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "RaytracingAO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

    RTScale = (float)(atof(gOptions.UseResolution) / 100.0f);
    RayRTWidth = (int32)(SCR_WIDTH * RTScale);
    RayRTHeight = (int32)(SCR_HEIGHT * RTScale);

    // Create Persistent Resources
    if (!jSceneRenderTarget::GaussianV || jSceneRenderTarget::GaussianV->Width != (int32)RayRTWidth || jSceneRenderTarget::GaussianV->Height != (int32)RayRTHeight)
    {
        jSceneRenderTarget::GaussianV = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
            , ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
    }
    if (!jSceneRenderTarget::GaussianH || jSceneRenderTarget::GaussianH->Width != (int32)RayRTWidth || jSceneRenderTarget::GaussianH->Height != (int32)RayRTHeight)
    {
        jSceneRenderTarget::GaussianH = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
            , ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
    }
    if (!jSceneRenderTarget::AOProjection || jSceneRenderTarget::AOProjection->Info.Width != (int32)RayRTWidth || jSceneRenderTarget::AOProjection->Info.Height != (int32)RayRTHeight)
    {
        jSceneRenderTarget::AOProjection = g_rhi->CreateRenderTarget({ ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, RayRTWidth, RayRTHeight, 1, false, g_rhi->GetSelectedMSAASamples()
            , jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f), ETextureCreateFlag::UAV });
    }
    if (!jSceneRenderTarget::HistoryBuffer || jSceneRenderTarget::HistoryBuffer->Width != (int32)RayRTWidth || jSceneRenderTarget::HistoryBuffer->Height != (int32)RayRTHeight)
    {
        jSceneRenderTarget::HistoryBuffer = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
            , ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
    }
    if (RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr)
    {
        const int32 DepthWidth = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width;
        const int32 DepthHeight = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height;
        if (!jSceneRenderTarget::HistoryDepthBuffer || jSceneRenderTarget::HistoryDepthBuffer->Width != DepthWidth || jSceneRenderTarget::HistoryDepthBuffer->Height != DepthHeight)
        {
            jSceneRenderTarget::HistoryDepthBuffer = g_rhi->Create2DTexture((uint32)DepthWidth, (uint32)DepthHeight, (uint32)1, (uint32)1
                , ETextureFormat::R8, ETextureCreateFlag::UAV, EResourceLayout::UAV);
        }
    }

    // 1. RTAO ray shoot
    {
        auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();
        const jSamplerStateInfo* PBRSamplerStateInfo = TSamplerStateInfo<ETextureFilter::NEAREST_MIPMAP_LINEAR, ETextureFilter::NEAREST_MIPMAP_LINEAR
            , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

        struct SceneConstantBuffer
        {
            Matrix projectionToWorld;
            Vector4 ViewRect;
            Vector cameraPosition;
            float focalDistance;
            Vector lightPosition;
            float lensRadius;
            Vector lightAmbientColor;
            uint32 NumOfStartingRay;
            Vector lightDiffuseColor;
            int32 FrameNumber;
            Vector cameraDirection;
            float AORadius;
            Vector lightDirection;
            uint32 RayPerPixel;
            int32 Clear;
            float AOIntensity;
            int32 Padding2;
            float Padding0;            // for 16 byte align
            Vector2 HaltonJitter;
            Vector2 Padding1;          // for 16 byte align
        };

		SceneConstantBuffer m_sceneCB;
		auto mainCamera = jCamera::GetMainCamera();
		m_sceneCB.cameraPosition = mainCamera->Pos;
        m_sceneCB.projectionToWorld = mainCamera->GetViewProjectionMatrix().GetInverse();
        m_sceneCB.lightPosition = Vector(0.0f, 1.8f, -3.0f);
        m_sceneCB.lightAmbientColor = Vector(0.5f, 0.5f, 0.5f);
        m_sceneCB.lightDiffuseColor = Vector(0.5f, 0.3f, 0.3f);
        m_sceneCB.cameraDirection = (mainCamera->Target - mainCamera->Pos).GetNormalize();
        m_sceneCB.focalDistance = gOptions.FocalDistance;
        m_sceneCB.lensRadius = gOptions.LensRadius;
        m_sceneCB.NumOfStartingRay = 20;
        m_sceneCB.ViewRect = Vector4(0.0f, 0.0f, (float)RayRTWidth, (float)RayRTHeight);
        m_sceneCB.FrameNumber = (int32)g_rhi->GetCurrentFrameNumber();
        m_sceneCB.AORadius = gOptions.AORadius;
        m_sceneCB.AOIntensity = gOptions.AOIntensity;
        m_sceneCB.RayPerPixel = (uint32)Max(gOptions.RayPerPixel, 0);
        
        static Vector2 HaltonJitter[]={
            Vector2(0.0f,      -0.333334f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.5f,     0.333334f)  / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(0.5f,      -0.777778f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.75f,    -0.111112f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(0.25f,     0.555556f)  / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.25f,    -0.555556f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(0.75f,     0.111112f)  / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.875f,   0.777778f)  / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(0.125f,    -0.925926f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.375f,   -0.259260f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(0.625f,    0.407408f)  / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.625f,   -0.703704f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(0.375f,    -0.037038f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.125f,   0.629630f)  / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(0.875f,    -0.481482f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
            Vector2(-0.9375f,  0.185186f)  / Vector2((float)RayRTWidth, (float)RayRTHeight)
        };

        if (gOptions.UseHaltonJitter)
        {
            static int32 index = 0;
            m_sceneCB.HaltonJitter = HaltonJitter[index % _countof(HaltonJitter)];
            ++index;
        }
        else
        {
            m_sceneCB.HaltonJitter = Vector2::ZeroVector;
        }

        static jOptions OldOptions = gOptions;
        static auto OldMatrix = m_sceneCB.projectionToWorld;
        if (!gOptions.UseAccumulateRay || OldMatrix != m_sceneCB.projectionToWorld || OldOptions != gOptions)
        {
            OldMatrix = m_sceneCB.projectionToWorld;
            memcpy(&OldOptions, &gOptions, sizeof(gOptions));
            m_sceneCB.Clear = true;
        }
        else
        {
            m_sceneCB.Clear = false;
        }

        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "DispatchRayAO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

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
            m_sceneCB.lightDirection = { DirectionalLight->GetLightData().Direction.x, DirectionalLight->GetLightData().Direction.y, DirectionalLight->GetLightData().Direction.z };

            auto SceneUniformBufferPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("SceneData"), jLifeTimeType::OneFrame, sizeof(m_sceneCB));
            SceneUniformBufferPtr->UpdateBufferData(&m_sceneCB, sizeof(m_sceneCB));

            if (!RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr 
                || RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Width != (int32)RayRTWidth
                || RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Height != (int32)RayRTHeight)
            {
                RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
                    , ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
            }

            // Normal resource
            // Record normal resources
            jShaderBindingArray ShaderBindingArray;
            jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
            ShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::ACCELERATION_STRUCTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jBufferResource>(RenderFrameContextPtr->RaytracingScene->TLASBufferPtr.get()), true));
            ShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get(), nullptr), false));
            ShaderBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[0]->GetTexture(), nullptr), false));
            ShaderBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[1]->GetTexture(), nullptr), false));
            ShaderBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jUniformBufferResource>(SceneUniformBufferPtr.get()), true));
            ShaderBindingArray.Add(jShaderBinding::Create(5, 1, EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jSamplerResource>(SamplerState), false));
            ShaderBindingArray.Add(jShaderBinding::Create(6, 1, EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jSamplerResource>(PBRSamplerStateInfo), false));

    #define TURN_ON_BINDLESS 1

    #if TURN_ON_BINDLESS
            // Bindless resources
            // Record bindless resources
            jShaderBindingArray BindlessShaderBindingArray[9];

            std::vector<jTextureResourceBindless::jTextureBindData> IrradianceMapTextures;
            jTextureResourceBindless::jTextureBindData IrradianceTextureBindData;
            IrradianceTextureBindData.Texture = jSceneRenderTarget::IrradianceMap2;
            IrradianceMapTextures.push_back(IrradianceTextureBindData);

            std::vector<jTextureResourceBindless::jTextureBindData> FilteredEnvMapTextures;
            jTextureResourceBindless::jTextureBindData FilteredEnvMapBindData;
            FilteredEnvMapBindData.Texture = jSceneRenderTarget::FilteredEnvMap2;
            FilteredEnvMapTextures.push_back(FilteredEnvMapBindData);

            // Below Two textures have only 1 texture each, but this is just test code for bindless resouces so I will remove this from bindless resource range.
            BindlessShaderBindingArray[0].Add(jShaderBinding::CreateBindless(0, (uint32)IrradianceMapTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResourceBindless>(IrradianceMapTextures), false));
            BindlessShaderBindingArray[1].Add(jShaderBinding::CreateBindless(0, (uint32)FilteredEnvMapTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResourceBindless>(FilteredEnvMapTextures), false));

            std::vector<const jBuffer*> VertexAndInexOffsetBuffers;
            std::vector<const jBuffer*> IndexBuffers;
            std::vector<const jBuffer*> TestUniformBuffers;
            std::vector<const jBuffer*> VertexBuffers;
            std::vector<jTextureResourceBindless::jTextureBindData> AlbedoTextures;
            std::vector<jTextureResourceBindless::jTextureBindData> NormalTextures;
            std::vector<jTextureResourceBindless::jTextureBindData> MetallicTextures;

            for (int32 i = 0; i < jObject::GetStaticRenderObject().size(); ++i)
            {
                jRenderObject* RObj = jObject::GetStaticRenderObject()[i];
                RObj->CreateShaderBindingInstance();

                VertexAndInexOffsetBuffers.push_back(RObj->VertexAndIndexOffsetBuffer.get());
                IndexBuffers.push_back(RObj->GeometryDataPtr->IndexBufferPtr->GetBuffer());
                TestUniformBuffers.push_back(RObj->TestUniformBuffer.get());
                VertexBuffers.push_back(RObj->GeometryDataPtr->VertexBufferPtr->GetBuffer(0));

                check(RObj->MaterialPtr);
                AlbedoTextures.push_back(jTextureResourceBindless::jTextureBindData(RObj->MaterialPtr->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Albedo), nullptr));
                NormalTextures.push_back(jTextureResourceBindless::jTextureBindData(RObj->MaterialPtr->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Normal), nullptr));
                MetallicTextures.push_back(jTextureResourceBindless::jTextureBindData(RObj->MaterialPtr->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Metallic), nullptr));
            }
            BindlessShaderBindingArray[2].Add(jShaderBinding::CreateBindless(0, (uint32)VertexAndInexOffsetBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jBufferResourceBindless>(VertexAndInexOffsetBuffers), false));
            BindlessShaderBindingArray[3].Add(jShaderBinding::CreateBindless(0, (uint32)IndexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jBufferResourceBindless>(IndexBuffers), false));
            BindlessShaderBindingArray[4].Add(jShaderBinding::CreateBindless(0, (uint32)TestUniformBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jBufferResourceBindless>(TestUniformBuffers), false));
            BindlessShaderBindingArray[5].Add(jShaderBinding::CreateBindless(0, (uint32)VertexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jBufferResourceBindless>(VertexBuffers), false));
            BindlessShaderBindingArray[6].Add(jShaderBinding::CreateBindless(0, (uint32)AlbedoTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResourceBindless>(AlbedoTextures)));
            BindlessShaderBindingArray[7].Add(jShaderBinding::CreateBindless(0, (uint32)NormalTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResourceBindless>(NormalTextures)));
            BindlessShaderBindingArray[8].Add(jShaderBinding::CreateBindless(0, (uint32)MetallicTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
                ResourceInlineAllactor.Alloc<jTextureResourceBindless>(MetallicTextures)));
    #endif // TURN_ON_BINDLESS

            // Create ShaderBindingLayout and ShaderBindingInstance Instance for this draw call
            std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstance;
            GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
    #if TURN_ON_BINDLESS
            std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstanceBindless[9];
            for (int32 i = 0; i < 9; ++i)
            {
                GlobalShaderBindingInstanceBindless[i] = g_rhi->CreateShaderBindingInstance(BindlessShaderBindingArray[i], jShaderBindingInstanceType::SingleFrame);
            }
    #endif // TURN_ON_BINDLESS

            jShaderBindingLayoutArray GlobalShaderBindingLayoutArray;
            GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);
    #if TURN_ON_BINDLESS
            for (int32 i = 0; i < 9; ++i)
            {
                GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstanceBindless[i]->ShaderBindingsLayouts);
            }
    #endif // TURN_ON_BINDLESS

            // Create RaytracingShaders
            std::vector<jRaytracingPipelineShader> RaytracingShaders;
            {
                jRaytracingPipelineShader NewShader;
                jShaderInfo shaderInfo;

                // First hit gorup
                shaderInfo.SetName(jNameStatic("Miss"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
                shaderInfo.SetEntryPoint(jNameStatic("MyMissShader"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_MISS);
    #if TURN_ON_BINDLESS
                shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
    #endif // TURN_ON_BINDLESS
                NewShader.MissShader = g_rhi->CreateShader(shaderInfo);
                NewShader.MissEntryPoint = TEXT("MyMissShader");

                shaderInfo.SetName(jNameStatic("Raygen"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
                shaderInfo.SetEntryPoint(jNameStatic("MyRaygenShader"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_RAYGEN);
    #if TURN_ON_BINDLESS
                shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
    #endif
                NewShader.RaygenShader = g_rhi->CreateShader(shaderInfo);
                NewShader.RaygenEntryPoint = TEXT("MyRaygenShader");

                shaderInfo.SetName(jNameStatic("ClosestHit"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
                shaderInfo.SetEntryPoint(jNameStatic("MyClosestHitShader"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT);
    #if TURN_ON_BINDLESS
                shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
    #endif // TURN_ON_BINDLESS
                NewShader.ClosestHitShader = g_rhi->CreateShader(shaderInfo);
                NewShader.ClosestHitEntryPoint = TEXT("MyClosestHitShader");

                shaderInfo.SetName(jNameStatic("AnyHit"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
                shaderInfo.SetEntryPoint(jNameStatic("MyAnyHitShader"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_ANYHIT);
    #if TURN_ON_BINDLESS
                shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
    #endif // TURN_ON_BINDLESS
                NewShader.AnyHitShader = g_rhi->CreateShader(shaderInfo);
                NewShader.AnyHitEntryPoint = TEXT("MyAnyHitShader");

                NewShader.HitGroupName = TEXT("DefaultHit");

                RaytracingShaders.push_back(NewShader);
            }

            // Create RaytracingPipelineState
            jRaytracingPipelineData RaytracingPipelineData;
            RaytracingPipelineData.MaxAttributeSize = 2 * sizeof(float);	                // float2 barycentrics
            RaytracingPipelineData.MaxPayloadSize = 4 * sizeof(float);		                    // float shadow
            RaytracingPipelineData.MaxTraceRecursionDepth = 1;
            auto RaytracingPipelineState = g_rhi->CreateRaytracingPipelineStateInfo(RaytracingShaders, RaytracingPipelineData
                , GlobalShaderBindingLayoutArray, nullptr);

            // Binding RaytracingShader resources
            jShaderBindingInstanceArray ShaderBindingInstanceArray;
            ShaderBindingInstanceArray.Add(GlobalShaderBindingInstance.get());
    #if TURN_ON_BINDLESS
            for (int32 i = 0; i < _countof(GlobalShaderBindingInstanceBindless); ++i)
            {
                ShaderBindingInstanceArray.Add(GlobalShaderBindingInstanceBindless[i].get());
            }
    #endif // TURN_ON_BINDLESS

            jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
            for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
            {
                ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
                const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
                if (pDynamicOffsetTest && pDynamicOffsetTest->size())
                {
                    ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
                }
            }
            ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;
            g_rhi->BindRaytracingShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), RaytracingPipelineState, ShaderBindingInstanceCombiner, 0);

            // Binding Raytracing Pipeline State
            RaytracingPipelineState->Bind(RenderFrameContextPtr);

            g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get(), EResourceLayout::UAV);
            g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[0]->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[1]->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            // Dispatch Rays
            jRaytracingDispatchData TracingData;
            TracingData.Width = RayRTWidth;
            TracingData.Height = RayRTHeight;
            TracingData.Depth = 1;
            TracingData.PipelineState = RaytracingPipelineState;
            g_rhi->DispatchRay(RenderFrameContextPtr, TracingData);

            g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get(), EResourceLayout::SHADER_READ_ONLY);
        }
    }

    // 2. Reprojection AO
    std::shared_ptr<jTexture> AfterReprojection = ReprojectionAO(RenderFrameContextPtr, RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr);
    
    if (gOptions.ShowDebugRT)
        DebugRTs.push_back(AfterReprojection);

    // 3. Denosing
    std::shared_ptr<jTexture> AfterDenoising = DenoisingAO(RenderFrameContextPtr, AfterReprojection);
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), AfterDenoising.get());

    if (gOptions.ShowDebugRT)
        DebugRTs.push_back(AfterDenoising);

    // 4. UpdateHistoryBuffer
    std::shared_ptr<jTexture> AfterUpdateHistoryBuffer = UpdateHistoryBuffer(RenderFrameContextPtr, AfterDenoising);

	// 5. Apply AO to Final color 
    {
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "Apply AO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(ApplyAO);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, ApplyAO);

        struct CommonComputeUniformBuffer
        {
            int32 Width;
            int32 Height;
            float AOIntensity;
            int32 Padding0;
        };
        CommonComputeUniformBuffer CommonComputeData;
        CommonComputeData.Width = AfterUpdateHistoryBuffer->Width;
        CommonComputeData.Height = AfterUpdateHistoryBuffer->Height;
        CommonComputeData.AOIntensity = gOptions.AOIntensity;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("OnFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        bool IsApplyAOCompute = false;
        if (IsApplyAOCompute)
	    {
            jRHIUtil::DispatchCompute(RenderFrameContextPtr, RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()
			    , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
			    {
				    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), AfterUpdateHistoryBuffer.get(), EResourceLayout::SHADER_READ_ONLY);

				    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE
					    , InOutResourceInlineAllactor.Alloc<jTextureResource>(AfterUpdateHistoryBuffer.get(), nullptr)));

				    InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
					    , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
			    }
			    , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
			    {
				    jShaderInfo shaderInfo;
				    shaderInfo.SetName(jNameStatic("AOApplyCS"));
				    shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AOApply_cs.hlsl"));
				    shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                    shaderInfo.SetEntryPoint(jNameStatic("AOApplyCS"));
				    if (gOptions.ShowAOOnly)
					    shaderInfo.SetPreProcessors(jNameStatic("#define SHOW_AO_ONLY 1"));
				    jShader* Shader = g_rhi->CreateShader(shaderInfo);
				    return Shader;
			    }
            );
	    }
        else
        {
            jRHIUtil::DrawFullScreen(RenderFrameContextPtr, RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr
            , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
            {
                const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                    , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                    , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

                g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), AfterUpdateHistoryBuffer.get(), EResourceLayout::SHADER_READ_ONLY);

                InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                    , InOutResourceInlineAllactor.Alloc<jTextureResource>(AfterUpdateHistoryBuffer.get(), SamplerState)));

                InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
                    , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
            }
            , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                {
                    jShaderInfo shaderInfo;
                    shaderInfo.SetName(jNameStatic("AOApplyPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AOApply_cs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    shaderInfo.SetEntryPoint(jNameStatic("AOApplyPS"));
                    if (gOptions.ShowAOOnly)
                        shaderInfo.SetPreProcessors(jNameStatic("#define SHOW_AO_ONLY 1"));
                    jShader* Shader = g_rhi->CreateShader(shaderInfo);
                    return Shader;
                }
            , [](jRasterizationStateInfo*& OutRasterState, jBlendingStateInfo*& OutBlendState, jDepthStencilStateInfo*& OutDepthStencilState)
                {
                    OutRasterState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
                    OutDepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
                    
                    if (gOptions.ShowAOOnly)
                        OutBlendState = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();
                    else
                        OutBlendState = TBlendingStateInfo<true, EBlendFactor::ZERO, EBlendFactor::SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();
                });

        }
    }
}
