#include "pch.h"
#include "jPipelineStateInfo_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jVertexBuffer_Vulkan.h"
#include "jShader_Vulkan.h"

void jSamplerStateInfo_Vulkan::Initialize()
{
    SamplerStateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerStateInfo.magFilter = GetVulkanTextureFilterType(Magnification);
    SamplerStateInfo.minFilter = GetVulkanTextureFilterType(Minification);

    // UV가 [0~1] 범위를 벗어는 경우 처리
    // VK_SAMPLER_ADDRESS_MODE_REPEAT : 반복해서 출력, UV % 1
    // VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : 반복하지만 거울에 비치듯 반대로 출력
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : 범위 밖은 가장자리의 색으로 모두 출력
    // VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : 범위 밖은 반대편 가장자리의 색으로 모두 출력
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : 단색으로 설정함. (samplerInfo.borderColor)
    SamplerStateInfo.addressModeU = GetVulkanTextureAddressMode(AddressU);
    SamplerStateInfo.addressModeV = GetVulkanTextureAddressMode(AddressV);
    SamplerStateInfo.addressModeW = GetVulkanTextureAddressMode(AddressW);

    SamplerStateInfo.anisotropyEnable = (MaxAnisotropy > 1);
    SamplerStateInfo.maxAnisotropy = MaxAnisotropy;

    // 이게 true 이면 UV 좌표가 [0, texWidth], [0, texHeight] 가 됨. false 이면 [0, 1] 범위
    SamplerStateInfo.unnormalizedCoordinates = VK_FALSE;

    // compareEnable이 ture 이면, 텍셀을 특정 값과 비교한 뒤 그 결과를 필터링 연산에 사용한다.
    // Percentage-closer filtering(PCF) 에 주로 사용됨.
    SamplerStateInfo.compareEnable = IsEnableComparisonMode;
    SamplerStateInfo.compareOp = GetVulkanCompareOp(ComparisonFunc);

    uint32 textureMipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(SCR_WIDTH, SCR_HEIGHT)))) + 1;		// 이것도 수정 필요. SamplerState 는 텍스쳐에 바인딩 해야 할듯 

    SamplerStateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerStateInfo.mipLodBias = 0.0f;		// Optional
    SamplerStateInfo.minLod = MinLOD;		// Optional
    SamplerStateInfo.maxLod = MaxLOD;

    SamplerStateInfo.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;

    VkSamplerCustomBorderColorCreateInfoEXT CustomBorderColor{};
    CustomBorderColor.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
    memcpy(CustomBorderColor.customBorderColor.float32, &BorderColor, sizeof(BorderColor));
    SamplerStateInfo.pNext = &CustomBorderColor;

    ensure(vkCreateSampler(g_rhi_vk->Device, &SamplerStateInfo, nullptr, &SamplerState) == VK_SUCCESS);

    ResourceName = jName(ToString().c_str());
}

void jSamplerStateInfo_Vulkan::Release()
{
    if (SamplerState)
    {
        vkDestroySampler(g_rhi_vk->Device, SamplerState, nullptr);
        SamplerState = nullptr;
    }
}

void jRasterizationStateInfo_Vulkan::Initialize()
{
    RasterizationStateInfo = {};
    RasterizationStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterizationStateInfo.depthClampEnable = DepthClampEnable;						// 이 값이 VK_TRUE 면 Near나 Far을 벗어나는 영역을 [0.0 ~ 1.0]으로 Clamp 시켜줌.(쉐도우맵에서 유용)
    RasterizationStateInfo.rasterizerDiscardEnable = RasterizerDiscardEnable;		// 이 값이 VK_TRUE 면, 레스터라이저 스테이지를 통과할 수 없음. 즉 Framebuffer 로 결과가 넘어가지 않음.
    RasterizationStateInfo.polygonMode = GetVulkanPolygonMode(PolygonMode);			// FILL, LINE, POINT 세가지가 있음
    RasterizationStateInfo.lineWidth = LineWidth;
    RasterizationStateInfo.cullMode = GetVulkanCullMode(CullMode);
    if (IsUse_VULKAN_NDC_Y_FLIP())
    {
        RasterizationStateInfo.frontFace = GetVulkanFrontFace((EFrontFace::CCW == FrontFace) ? EFrontFace::CCW : EFrontFace::CW);
    }
    else
    {
        RasterizationStateInfo.frontFace = GetVulkanFrontFace(FrontFace);
    }
    RasterizationStateInfo.depthBiasEnable = DepthBiasEnable;						// 쉐도우맵 용
    RasterizationStateInfo.depthBiasConstantFactor = DepthBiasConstantFactor;		// Optional
    RasterizationStateInfo.depthBiasClamp = DepthBiasClamp;							// Optional
    RasterizationStateInfo.depthBiasSlopeFactor = DepthBiasSlopeFactor;				// Optional

    // VkPipelineRasterizationStateCreateFlags flags;

    MultisampleStateInfo = {};
    MultisampleStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MultisampleStateInfo.rasterizationSamples = (VkSampleCountFlagBits)SampleCount;
#if USE_VARIABLE_SHADING_RATE_TIER2
    MultisampleStateInfo.sampleShadingEnable = SampleShadingEnable;			// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
#else
    MultisampleStateInfo.sampleShadingEnable = false;
#endif
    MultisampleStateInfo.minSampleShading = MinSampleShading;
    MultisampleStateInfo.alphaToCoverageEnable = AlphaToCoverageEnable;		// Optional
    MultisampleStateInfo.alphaToOneEnable = AlphaToOneEnable;				// Optional
}

void jStencilOpStateInfo_Vulkan::Initialize()
{
    StencilOpStateInfo = {};
    StencilOpStateInfo.failOp = GetVulkanStencilOp(FailOp);
    StencilOpStateInfo.passOp = GetVulkanStencilOp(PassOp);
    StencilOpStateInfo.depthFailOp = GetVulkanStencilOp(DepthFailOp);
    StencilOpStateInfo.compareOp = GetVulkanCompareOp(CompareOp);
    StencilOpStateInfo.compareMask = CompareMask;
    StencilOpStateInfo.writeMask = WriteMask;
    StencilOpStateInfo.reference = Reference;
}

void jDepthStencilStateInfo_Vulkan::Initialize()
{
    DepthStencilStateInfo = {};
    DepthStencilStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthStencilStateInfo.depthTestEnable = DepthTestEnable;
    DepthStencilStateInfo.depthWriteEnable = DepthWriteEnable;
    DepthStencilStateInfo.depthCompareOp = GetVulkanCompareOp(DepthCompareOp);
    DepthStencilStateInfo.depthBoundsTestEnable = DepthBoundsTestEnable;
    DepthStencilStateInfo.minDepthBounds = MinDepthBounds;		// Optional
    DepthStencilStateInfo.maxDepthBounds = MaxDepthBounds;		// Optional
    DepthStencilStateInfo.stencilTestEnable = StencilTestEnable;
    if (Front)
    {
        DepthStencilStateInfo.front = ((jStencilOpStateInfo_Vulkan*)Front)->StencilOpStateInfo;
    }
    if (Back)
    {
        DepthStencilStateInfo.back = ((jStencilOpStateInfo_Vulkan*)Back)->StencilOpStateInfo;
    }
}

void jBlendingStateInfo_Vulkan::Initialize()
{
    ColorBlendAttachmentInfo = {};
    ColorBlendAttachmentInfo.blendEnable = BlendEnable;
    ColorBlendAttachmentInfo.srcColorBlendFactor = GetVulkanBlendFactor(Src);
    ColorBlendAttachmentInfo.dstColorBlendFactor = GetVulkanBlendFactor(Dest);
    ColorBlendAttachmentInfo.colorBlendOp = GetVulkanBlendOp(BlendOp);
    ColorBlendAttachmentInfo.srcAlphaBlendFactor = GetVulkanBlendFactor(SrcAlpha);
    ColorBlendAttachmentInfo.dstAlphaBlendFactor = GetVulkanBlendFactor(DestAlpha);
    ColorBlendAttachmentInfo.alphaBlendOp = GetVulkanBlendOp(AlphaBlendOp);
    ColorBlendAttachmentInfo.colorWriteMask = GetVulkanColorMask(ColorWriteMask);
}

void jPipelineStateInfo_Vulkan::Release()
{
    if (vkPipeline)
    {
        vkDestroyPipeline(g_rhi_vk->Device, vkPipeline, nullptr);
        vkPipeline = nullptr;
    }
    vkPipelineLayout = nullptr;
}

void jPipelineStateInfo_Vulkan::Initialize()
{
    if (PipelineType == EPipelineType::Graphics)
        CreateGraphicsPipelineState();
    else if (PipelineType == EPipelineType::Compute)
        CreateComputePipelineState();
    else if (PipelineType == EPipelineType::RayTracing)
        CreateRaytracingPipelineState();
    else
        check(0);
}

//////////////////////////////////////////////////////////////////////////
// jPipelineStateInfo_Vulkan
//////////////////////////////////////////////////////////////////////////
void* jPipelineStateInfo_Vulkan::CreateGraphicsPipelineState()
{
    // 미리 만들어 둔게 있으면 사용
    if (vkPipeline)
        return vkPipeline;

    check(PipelineStateFixed);
    check(VertexBufferArray.NumOfData);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo2 = ((jVertexBuffer_Vulkan*)VertexBufferArray[0])->CreateVertexInputState();

    // vkCreateGraphicsPipelines 호출 전까지 아래 bindingDescriptions, attributeDescriptions 소멸해제 안되야 함.
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    jVertexBuffer_Vulkan::CreateVertexInputState(vertexInputInfo, bindingDescriptions, attributeDescriptions, VertexBufferArray);

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = ((jVertexBuffer_Vulkan*)VertexBufferArray[0])->CreateInputAssemblyState();

    // 4. Viewports and scissors
    // SwapChain의 이미지 사이즈가 이 클래스에 정의된 상수 WIDTH, HEIGHT와 다를 수 있다는 것을 기억 해야함.
    // 그리고 Viewports 사이즈는 SwapChain 크기 이하로 마추면 됨.
    // [minDepth ~ maxDepth] 는 [0.0 ~ 1.0] 이며 특별한 경우가 아니면 이 범위로 사용하면 된다.
    // Scissor Rect 영역을 설정해주면 영역내에 있는 Pixel만 레스터라이저를 통과할 수 있으며 나머지는 버려(Discard)진다.
    const auto& Viewports = PipelineStateFixed->Viewports;
    std::vector<VkViewport> vkViewports;
    vkViewports.resize(Viewports.size());
    for (int32 i = 0; i < vkViewports.size(); ++i)
    {
        vkViewports[i].x = Viewports[i].X;
        vkViewports[i].width = Viewports[i].Width;
        
        if (IsUse_VULKAN_NDC_Y_FLIP())
        {
            vkViewports[i].y = Viewports[i].Height - Viewports[i].Y;
            vkViewports[i].height = -Viewports[i].Height;
        }
        else
        {
            vkViewports[i].y = Viewports[i].Y;
            vkViewports[i].height = Viewports[i].Height;
        }

        vkViewports[i].minDepth = Viewports[i].MinDepth;
        vkViewports[i].maxDepth = Viewports[i].MaxDepth;
    }

    const auto& Scissors = PipelineStateFixed->Scissors;
    std::vector<VkRect2D> vkScissor;
    vkScissor.resize(Scissors.size());
    for (int32 i = 0; i < vkScissor.size(); ++i)
    {
        vkScissor[i].offset.x = Scissors[i].Offset.x;
        vkScissor[i].offset.y = Scissors[i].Offset.y;
        vkScissor[i].extent.width = Scissors[i].Extent.x;
        vkScissor[i].extent.height = Scissors[i].Extent.y;
    }

    // Viewport와 Scissor 를 여러개 설정할 수 있는 멀티 뷰포트를 사용할 수 있기 때문임
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = Max<uint32>(1, (uint32)vkViewports.size());
    viewportState.pViewports = vkViewports.size() ? &vkViewports[0] : nullptr;
    viewportState.scissorCount = Max<uint32>(1, (uint32)vkScissor.size());
    viewportState.pScissors = vkScissor.size() ? &vkScissor[0] : nullptr;

    // 2가지 blending 방식을 모두 끌 수도있는데 그렇게 되면, 변경되지 않은 fragment color 값이 그대로 framebuffer에 쓰여짐.
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    // 2). 기존과 새로운 값을 비트 연산으로 결합한다.
    colorBlending.logicOpEnable = VK_FALSE;			// 모든 framebuffer에 사용하는 blendEnable을 VK_FALSE로 했다면 자동으로 logicOpEnable은 꺼진다.
    colorBlending.logicOp = VK_LOGIC_OP_COPY;		// Optional

    // 1). 기존과 새로운 값을 섞어서 최종색을 만들어낸다.
    // 사용하는 ColorAttachment 의 숫자를 계산
    int32 ColorAttachmentCountInSubpass = 0;
    check(RenderPass->RenderPassInfo.Subpasses.size() > SubpassIndex);
    const jSubpass& SelectedSubpass = RenderPass->RenderPassInfo.Subpasses[SubpassIndex];
    for (int32 i = 0; i < (int32)SelectedSubpass.OutputColorAttachments.size(); ++i)
    {
        const int32 AttchmentIndex = SelectedSubpass.OutputColorAttachments[i];
        const bool IsColorAttachment = !RenderPass->RenderPassInfo.Attachments[AttchmentIndex].IsDepthAttachment() &&
            !RenderPass->RenderPassInfo.Attachments[AttchmentIndex].IsResolveAttachment();
        if (IsColorAttachment)
            ++ColorAttachmentCountInSubpass;
    }
    // todo : 모든 ColorAttachment 에 동일한 ColorBlend 를 설정하는데 개별 설정하는 것이 필요하면 작업해야 함
    std::vector<VkPipelineColorBlendAttachmentState> ColorBlendAttachmentStates;
    if (ColorAttachmentCountInSubpass > 1)
    {
        ColorBlendAttachmentStates.resize(ColorAttachmentCountInSubpass, ((jBlendingStateInfo_Vulkan*)PipelineStateFixed->BlendingState)->ColorBlendAttachmentInfo);
        colorBlending.attachmentCount = ColorAttachmentCountInSubpass;
        colorBlending.pAttachments = &ColorBlendAttachmentStates[0];
    }
    else
    {
        colorBlending.attachmentCount = 1;						// framebuffer 개수에 맞게 설정해준다.
        colorBlending.pAttachments = &((jBlendingStateInfo_Vulkan*)PipelineStateFixed->BlendingState)->ColorBlendAttachmentInfo;
    }

    colorBlending.blendConstants[0] = 0.0f;		// Optional
    colorBlending.blendConstants[1] = 0.0f;		// Optional
    colorBlending.blendConstants[2] = 0.0f;		// Optional
    colorBlending.blendConstants[3] = 0.0f;		// Optional

    // 9. Dynamic state
    // 이전에 정의한 state에서 제한된 범위 내에서 새로운 pipeline을 만들지 않고 state를 변경할 수 있음. (viewport size, line width, blend constants)
    // 이것을 하고싶으면 Dynamic state를 만들어야 함. 이경우 Pipeline에 설정된 값은 무시되고, 매 렌더링시에 새로 설정해줘야 함.
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

    std::vector<VkDynamicState> dynamicStates;
    if (PipelineStateFixed->DynamicStates.size() > 0)
    {
        dynamicStates.resize(PipelineStateFixed->DynamicStates.size());

        for (int32 i=0;i<(int32)PipelineStateFixed->DynamicStates.size();++i)
        {
            dynamicStates[i] = GetVulkanPipelineDynamicState(PipelineStateFixed->DynamicStates[i]);
        }        

        dynamicState.dynamicStateCount = (uint32)dynamicStates.size();
        dynamicState.pDynamicStates = &dynamicStates[0];
    }

    // 10. Pipeline layout
    vkPipelineLayout = jShaderBindingLayout_Vulkan::CreatePipelineLayout(ShaderBindingLayoutArray, PushConstant);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    // Shader stage
    VkPipelineShaderStageCreateInfo ShaderStages[5];
    uint32 ShaderStageIndex = 0;
    if (GraphicsShader.VertexShader)
        ShaderStages[ShaderStageIndex++] = ((jCompiledShader_Vulkan*)GraphicsShader.VertexShader->GetCompiledShader())->ShaderStage;
    if (GraphicsShader.GeometryShader)
        ShaderStages[ShaderStageIndex++] = ((jCompiledShader_Vulkan*)GraphicsShader.GeometryShader->GetCompiledShader())->ShaderStage;
    if (GraphicsShader.PixelShader)
        ShaderStages[ShaderStageIndex++] = ((jCompiledShader_Vulkan*)GraphicsShader.PixelShader->GetCompiledShader())->ShaderStage;

    check(ShaderStageIndex > 0);
    pipelineInfo.stageCount = ShaderStageIndex;
    pipelineInfo.pStages = &ShaderStages[0];

    // Fixed-function stage
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &((jRasterizationStateInfo_Vulkan*)PipelineStateFixed->RasterizationState)->RasterizationStateInfo;
    pipelineInfo.pMultisampleState = &((jRasterizationStateInfo_Vulkan*)PipelineStateFixed->RasterizationState)->MultisampleStateInfo;
    pipelineInfo.pDepthStencilState = &((jDepthStencilStateInfo_Vulkan*)PipelineStateFixed->DepthStencilState)->DepthStencilStateInfo;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = vkPipelineLayout;
    pipelineInfo.renderPass = (VkRenderPass)RenderPass->GetRenderPass();
    pipelineInfo.subpass = SubpassIndex;		// index of subpass

    // Pipeline을 상속받을 수 있는데, 아래와 같은 장점이 있다.
    // 1). 공통된 내용이 많으면 파이프라인 설정이 저렴하다.
    // 2). 공통된 부모를 가진 파이프라인 들끼리의 전환이 더 빠를 수 있다.
    // BasePipelineHandle 이나 BasePipelineIndex 가 로 Pipeline 내용을 상속받을 수 있다.
    // 이 기능을 사용하려면 VkGraphicsPipelineCreateInfo의 flags 에 VK_PIPELINE_CREATE_DERIVATIVE_BIT  가 설정되어있어야 함
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;		// Optional
    pipelineInfo.basePipelineIndex = -1;					// Optional

    VkShadingRatePaletteNV shadingRatePalette{};
    VkPipelineViewportShadingRateImageStateCreateInfoNV pipelineViewportShadingRateImageStateCI{};
    
    static const std::vector<VkShadingRatePaletteEntryNV> shadingRatePaletteEntries = {
        VK_SHADING_RATE_PALETTE_ENTRY_NO_INVOCATIONS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_16_INVOCATIONS_PER_PIXEL_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_8_INVOCATIONS_PER_PIXEL_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_4_INVOCATIONS_PER_PIXEL_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_2_INVOCATIONS_PER_PIXEL_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X1_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_1X2_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X2_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X4_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV,
    };

     if (PipelineStateFixed->IsUseVRS)
    {
        // Shading Rate Palette
        shadingRatePalette.shadingRatePaletteEntryCount = static_cast<uint32_t>(shadingRatePaletteEntries.size());
        shadingRatePalette.pShadingRatePaletteEntries = shadingRatePaletteEntries.data();

        pipelineViewportShadingRateImageStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SHADING_RATE_IMAGE_STATE_CREATE_INFO_NV;
        pipelineViewportShadingRateImageStateCI.shadingRateImageEnable = VK_TRUE;
        pipelineViewportShadingRateImageStateCI.viewportCount = viewportState.viewportCount;
        pipelineViewportShadingRateImageStateCI.pShadingRatePalettes = &shadingRatePalette;
        viewportState.pNext = &pipelineViewportShadingRateImageStateCI;
    }

    // 여기서 두번째 파라메터 VkPipelineCache에 VK_NULL_HANDLE을 넘겼는데, VkPipelineCache는 VkPipeline을 저장하고 생성하는데 재사용할 수 있음.
    // 또한 파일로드 저장할 수 있어서 다른 프로그램에서 다시 사용할 수도있다. VkPipelineCache를 사용하면 VkPipeline을 생성하는 시간을 
    // 굉장히 빠르게 할수있다. (듣기로는 대략 1/10 의 속도로 생성해낼 수 있다고 함)
    if (!ensure(vkCreateGraphicsPipelines(g_rhi_vk->Device, g_rhi_vk->PipelineCache, 1, &pipelineInfo, nullptr, &vkPipeline) == VK_SUCCESS))
    {
        return nullptr;
    }

    size_t hash = GetHash();
    if (ensure(hash))
    {
        if (GraphicsShader.VertexShader)
            jShader::gConnectedPipelineStateHash[GraphicsShader.VertexShader].push_back(hash);
        if (GraphicsShader.GeometryShader)
            jShader::gConnectedPipelineStateHash[GraphicsShader.GeometryShader].push_back(hash);
        if (GraphicsShader.PixelShader)
            jShader::gConnectedPipelineStateHash[GraphicsShader.PixelShader].push_back(hash);
    }

    return vkPipeline;
}

void* jPipelineStateInfo_Vulkan::CreateComputePipelineState()
{
    // 미리 만들어 둔게 있으면 사용
    if (vkPipeline)
        return vkPipeline;

    vkPipelineLayout = jShaderBindingLayout_Vulkan::CreatePipelineLayout(ShaderBindingLayoutArray, PushConstant);

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.layout = vkPipelineLayout;
    computePipelineCreateInfo.flags = 0;
    computePipelineCreateInfo.stage = ((jCompiledShader_Vulkan*)ComputeShader->GetCompiledShader())->ShaderStage;

    if (!ensure(VK_SUCCESS == vkCreateComputePipelines(g_rhi_vk->Device, g_rhi_vk->PipelineCache
        , 1, &computePipelineCreateInfo, nullptr, &vkPipeline)))
    {
        return nullptr;
    }

    size_t hash = GetHash();
    if (ensure(hash))
    {
        if (ComputeShader)
            jShader::gConnectedPipelineStateHash[ComputeShader].push_back(hash);
    }

    return vkPipeline;
}

void* jPipelineStateInfo_Vulkan::CreateRaytracingPipelineState()
{
    // 미리 만들어 둔게 있으면 사용
    if (vkPipeline)
        return vkPipeline;

    vkPipelineLayout = jShaderBindingLayout_Vulkan::CreatePipelineLayout(ShaderBindingLayoutArray, PushConstant);

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> raygenGroups;
    raygenGroups.reserve(RaytracingShaders.size() * 4);
    
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> hitGroups;
    hitGroups.reserve(RaytracingShaders.size() * 4);
    
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> missGroups;
    missGroups.reserve(RaytracingShaders.size() * 4);
    
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.reserve(RaytracingShaders.size() * 4);

    for (int32 i = 0; i < RaytracingShaders.size(); ++i)
    {
        if (RaytracingShaders[i].RaygenShader)
        {
            shaderStages.push_back(((jCompiledShader_Vulkan*)RaytracingShaders[i].RaygenShader->CompiledShader)->ShaderStage);

            VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroup.generalShader = static_cast<uint32>(shaderStages.size()) - 1;
            shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            raygenGroups.push_back(shaderGroup);
        }

        if (RaytracingShaders[i].ClosestHitShader || RaytracingShaders[i].AnyHitShader)
        {
            VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

            if (RaytracingShaders[i].ClosestHitShader)
            {
                shaderStages.push_back(((jCompiledShader_Vulkan*)RaytracingShaders[i].ClosestHitShader->CompiledShader)->ShaderStage);
                shaderGroup.closestHitShader = static_cast<uint32>(shaderStages.size()) - 1;;
            }

            if (RaytracingShaders[i].AnyHitShader)
            {
                shaderStages.push_back(((jCompiledShader_Vulkan*)RaytracingShaders[i].AnyHitShader->CompiledShader)->ShaderStage);
                shaderGroup.anyHitShader = static_cast<uint32>(shaderStages.size()) - 1;;
            }

            hitGroups.push_back(shaderGroup);
        }

        if (RaytracingShaders[i].MissShader)
        {
            shaderStages.push_back(((jCompiledShader_Vulkan*)RaytracingShaders[i].MissShader->CompiledShader)->ShaderStage);

            VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroup.generalShader = static_cast<uint32>(shaderStages.size()) - 1;
            shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            missGroups.push_back(shaderGroup);
        }
    }

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> MergedShaderGroups;
    MergedShaderGroups.reserve(raygenGroups.size() + missGroups.size() + hitGroups.size());
    MergedShaderGroups.insert(MergedShaderGroups.end(), raygenGroups.begin(), raygenGroups.end());
    MergedShaderGroups.insert(MergedShaderGroups.end(), missGroups.begin(), missGroups.end());
    MergedShaderGroups.insert(MergedShaderGroups.end(), hitGroups.begin(), hitGroups.end());

    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
    rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rayTracingPipelineCI.stageCount = static_cast<uint32>(shaderStages.size());
    rayTracingPipelineCI.pStages = shaderStages.data();
    rayTracingPipelineCI.groupCount = static_cast<uint32>(MergedShaderGroups.size());
    rayTracingPipelineCI.pGroups = MergedShaderGroups.data();
    rayTracingPipelineCI.maxPipelineRayRecursionDepth = RaytracingPipelineData.MaxTraceRecursionDepth;
    rayTracingPipelineCI.layout = vkPipelineLayout;
    check(VK_SUCCESS == g_rhi_vk->vkCreateRayTracingPipelinesKHR(g_rhi_vk->Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &vkPipeline));

    // ShaderBindingTable
    const uint32 handleSize = g_rhi_vk->RayTracingPipelineProperties.shaderGroupHandleSize;
    const uint32 handleSizeAligned = Align(g_rhi_vk->RayTracingPipelineProperties.shaderGroupHandleSize, g_rhi_vk->RayTracingPipelineProperties.shaderGroupHandleAlignment);
    const uint32 groupCount = static_cast<uint32>(MergedShaderGroups.size());
    const uint32 sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8> shaderHandleStorage(sbtSize);
    check(VK_SUCCESS == g_rhi_vk->vkGetRayTracingShaderGroupHandlesKHR(g_rhi_vk->Device, vkPipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

    auto getBufferDeviceAddress = [](VkBuffer buffer)
    {
        VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
        bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bufferDeviceAI.buffer = buffer;
        return g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &bufferDeviceAI);
    };

    auto getSbtEntryStridedDeviceAddressRegion = [&](jBuffer_Vulkan* buffer, uint32 handleCount)
    {
        const uint32 handleSizeAligned = Align(g_rhi_vk->RayTracingPipelineProperties.shaderGroupHandleSize, g_rhi_vk->RayTracingPipelineProperties.shaderGroupHandleAlignment);
        VkStridedDeviceAddressRegionKHR stridedDeviceAddressRegionKHR{};
        stridedDeviceAddressRegionKHR.deviceAddress = getBufferDeviceAddress(buffer->Buffer) + buffer->Offset;
        stridedDeviceAddressRegionKHR.stride = handleSizeAligned;
        stridedDeviceAddressRegionKHR.size = handleCount * stridedDeviceAddressRegionKHR.stride;
        return stridedDeviceAddressRegionKHR;
    };

    const uint64 AlignedBaseGroupHandleSize = Align(g_rhi_vk->RayTracingPipelineProperties.shaderGroupHandleSize
        , g_rhi_vk->RayTracingPipelineProperties.shaderGroupBaseAlignment);

    {
        // Create buffer to hold all shader handles for the SBT
        const uint32 handleCount = (uint32)raygenGroups.size();
        RaygenBuffer = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(AlignedBaseGroupHandleSize* handleCount, 0, EBufferCreateFlag::CPUAccess | EBufferCreateFlag::ShaderBindingTable, EResourceLayout::GENERAL);
        RaygenStridedDeviceAddressRegion = getSbtEntryStridedDeviceAddressRegion(RaygenBuffer.get(), handleCount);
        RaygenBuffer->Map();
    }

    {
        // Create buffer to hold all shader handles for the SBT
        const uint32 handleCount = (uint32)missGroups.size();
        MissBuffer = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(AlignedBaseGroupHandleSize * handleCount, 0, EBufferCreateFlag::CPUAccess | EBufferCreateFlag::ShaderBindingTable, EResourceLayout::GENERAL);
        MissStridedDeviceAddressRegion = getSbtEntryStridedDeviceAddressRegion(MissBuffer.get(), handleCount);
        MissBuffer->Map();
    }

    {
        // Create buffer to hold all shader handles for the SBT
        const uint32 handleCount = (uint32)hitGroups.size();
        HitGroupBuffer = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(AlignedBaseGroupHandleSize * handleCount, 0, EBufferCreateFlag::CPUAccess | EBufferCreateFlag::ShaderBindingTable, EResourceLayout::GENERAL);
        HitStridedDeviceAddressRegion = getSbtEntryStridedDeviceAddressRegion(HitGroupBuffer.get(), handleCount);
        HitGroupBuffer->Map();
    };

    memcpy(RaygenBuffer->Map(), shaderHandleStorage.data(), handleSize * raygenGroups.size());
    memcpy(MissBuffer->Map(), shaderHandleStorage.data() + handleSizeAligned * (raygenGroups.size()), handleSize * missGroups.size());
    memcpy(HitGroupBuffer->Map(), shaderHandleStorage.data() + handleSizeAligned * (raygenGroups.size() + missGroups.size()), handleSize * hitGroups.size());

    RaygenBuffer->Unmap();
    MissBuffer->Unmap();
    HitGroupBuffer->Unmap();

    size_t hash = GetHash();
    if (ensure(hash))
    {
        for (int32 i = 0; i < (int32)RaytracingShaders.size(); ++i)
        {
            if (RaytracingShaders[i].RaygenShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].RaygenShader].push_back(hash);
            if (RaytracingShaders[i].ClosestHitShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].ClosestHitShader].push_back(hash);
            if (RaytracingShaders[i].AnyHitShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].AnyHitShader].push_back(hash);
            if (RaytracingShaders[i].MissShader)
                jShader::gConnectedPipelineStateHash[RaytracingShaders[i].MissShader].push_back(hash);
        }
    }

    return vkPipeline;
}

void jPipelineStateInfo_Vulkan::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    check(vkPipeline);
    if (PipelineType == EPipelineType::Graphics)
        vkCmdBindPipeline((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
    else if (PipelineType == EPipelineType::Compute)
        vkCmdBindPipeline((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline);
    else if (PipelineType == EPipelineType::RayTracing)
        vkCmdBindPipeline((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkPipeline);
    else
        check(0);
}
