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
    SamplerStateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    SamplerStateInfo.anisotropyEnable = (MaxAnisotropy > 1);
    SamplerStateInfo.maxAnisotropy = MaxAnisotropy;

    // 이게 true 이면 UV 좌표가 [0, texWidth], [0, texHeight] 가 됨. false 이면 [0, 1] 범위
    SamplerStateInfo.unnormalizedCoordinates = VK_FALSE;

    // compareEnable이 ture 이면, 텍셀을 특정 값과 비교한 뒤 그 결과를 필터링 연산에 사용한다.
    // Percentage-closer filtering(PCF) 에 주로 사용됨.
    SamplerStateInfo.compareEnable = VK_FALSE;
    SamplerStateInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    uint32 textureMipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(SCR_WIDTH, SCR_HEIGHT)))) + 1;		// 이것도 수정 필요. SamplerState 는 텍스쳐에 바인딩 해야 할듯 

    SamplerStateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerStateInfo.mipLodBias = 0.0f;		// Optional
    SamplerStateInfo.minLod = MinLOD;		// Optional
    SamplerStateInfo.maxLod = MaxLOD;

    ensure(vkCreateSampler(g_rhi_vk->Device, &SamplerStateInfo, nullptr, &SamplerState) == VK_SUCCESS);
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
    RasterizationStateInfo.frontFace = GetVulkanFrontFace(FrontFace);
    RasterizationStateInfo.depthBiasEnable = DepthBiasEnable;						// 쉐도우맵 용
    RasterizationStateInfo.depthBiasConstantFactor = DepthBiasConstantFactor;		// Optional
    RasterizationStateInfo.depthBiasClamp = DepthBiasClamp;							// Optional
    RasterizationStateInfo.depthBiasSlopeFactor = DepthBiasSlopeFactor;				// Optional

    // VkPipelineRasterizationStateCreateFlags flags;
}

void jMultisampleStateInfo_Vulkan::Initialize()
{
    MultisampleStateInfo = {};
    MultisampleStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MultisampleStateInfo.rasterizationSamples = (VkSampleCountFlagBits)SampleCount;
    MultisampleStateInfo.sampleShadingEnable = SampleShadingEnable;			// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
    MultisampleStateInfo.minSampleShading = MinSampleShading;
    MultisampleStateInfo.alphaToCoverageEnable = AlphaToCoverageEnable;		// Optional
    MultisampleStateInfo.alphaToOneEnable = AlphaToOneEnable;				// Optional

    // VkPipelineMultisampleStateCreateFlags flags;
    // const VkSampleMask* pSampleMask;
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

    // VkPipelineDepthStencilStateCreateFlags    flags;
}

void jBlendingStateInfo_Vulakn::Initialize()
{
    ColorBlendAttachmentInfo = {};
    ColorBlendAttachmentInfo.blendEnable = BlendEnable;
    ColorBlendAttachmentInfo.srcColorBlendFactor = GetVulkanBlendFactor(Src);
    ColorBlendAttachmentInfo.dstColorBlendFactor = GetVulkanBlendFactor(Dest);
    ColorBlendAttachmentInfo.colorBlendOp = GetVulkanBlendOp(BlendOp);
    ColorBlendAttachmentInfo.srcAlphaBlendFactor = GetVulkanBlendFactor(SrcAlpha);
    ColorBlendAttachmentInfo.dstAlphaBlendFactor = GetVulkanBlendFactor(DestAlpha);
    ColorBlendAttachmentInfo.alphaBlendOp = GetVulkanBlendOp(AlphaBlendOp);
    ColorBlendAttachmentInfo.colorWriteMask = GetVulkanBlendOp(ColorWriteMask);
}

void jPipelineStateInfo_Vulkan::Initialize()
{
    CreateGraphicsPipelineState();
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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateVertexInputState();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateInputAssemblyState();

    // 4. Viewports and scissors
    // SwapChain의 이미지 사이즈가 이 클래스에 정의된 상수 WIDTH, HEIGHT와 다를 수 있다는 것을 기억 해야함.
    // 그리고 Viewports 사이즈는 SwapChain 크기 이하로 마추면 됨.
    // [minDepth ~ maxDepth] 는 [0.0 ~ 1.0] 이며 특별한 경우가 아니면 이 범위로 사용하면 된다.
    // Scissor Rect 영역을 설정해주면 영역내에 있는 Pixel만 레스터라이저를 통과할 수 있으며 나머지는 버려(Discard)진다.
    const auto& Viewports = PipelineStateFixed->Viewports;
    check(Viewports.size());
    std::vector<VkViewport> vkViewports;
    vkViewports.resize(Viewports.size());
    for (int32 i = 0; i < vkViewports.size(); ++i)
    {
        vkViewports[i].x = Viewports[i].X;
        vkViewports[i].y = Viewports[i].Y;
        vkViewports[i].width = Viewports[i].Width;
        vkViewports[i].height = Viewports[i].Height;
        vkViewports[i].minDepth = Viewports[i].MinDepth;
        vkViewports[i].maxDepth = Viewports[i].MaxDepth;
    }

    const auto& Scissors = PipelineStateFixed->Scissors;
    check(Scissors.size());
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
    viewportState.viewportCount = (uint32)vkViewports.size();
    viewportState.pViewports = &vkViewports[0];
    viewportState.scissorCount = (uint32)vkScissor.size();
    viewportState.pScissors = &vkScissor[0];

    // 2가지 blending 방식을 모두 끌 수도있는데 그렇게 되면, 변경되지 않은 fragment color 값이 그대로 framebuffer에 쓰여짐.
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    // 2). 기존과 새로운 값을 비트 연산으로 결합한다.
    colorBlending.logicOpEnable = VK_FALSE;			// 모든 framebuffer에 사용하는 blendEnable을 VK_FALSE로 했다면 자동으로 logicOpEnable은 꺼진다.
    colorBlending.logicOp = VK_LOGIC_OP_COPY;		// Optional

    // 1). 기존과 새로운 값을 섞어서 최종색을 만들어낸다.
    colorBlending.attachmentCount = 1;						// framebuffer 개수에 맞게 설정해준다.
    colorBlending.pAttachments = &((jBlendingStateInfo_Vulakn*)PipelineStateFixed->BlendingState)->ColorBlendAttachmentInfo;

    colorBlending.blendConstants[0] = 0.0f;		// Optional
    colorBlending.blendConstants[1] = 0.0f;		// Optional
    colorBlending.blendConstants[2] = 0.0f;		// Optional
    colorBlending.blendConstants[3] = 0.0f;		// Optional

    // 9. Dynamic state
    // 이전에 정의한 state에서 제한된 범위 내에서 새로운 pipeline을 만들지 않고 state를 변경할 수 있음. (viewport size, line width, blend constants)
    // 이것을 하고싶으면 Dynamic state를 만들어야 함. 이경우 Pipeline에 설정된 값은 무시되고, 매 렌더링시에 새로 설정해줘야 함.
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_LINE_WIDTH
    };
    // 현재는 사용하지 않음.
    //VkPipelineDynamicStateCreateInfo dynamicState = {};
    //dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    //dynamicState.dynamicStateCount = 2;
    //dynamicState.pDynamicStates = dynamicStates;

    // 10. Pipeline layout
    vkPipelineLayout = (VkPipelineLayout)g_rhi->CreatePipelineLayout(ShaderBindings);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    // Shader stage
    check(Shader);
    //pipelineInfo.stageCount = 2;
    //pipelineInfo.pStages = shaderStage;
    pipelineInfo.stageCount = (uint32)((jShader_Vulkan*)Shader)->ShaderStages.size();
    pipelineInfo.pStages = ((jShader_Vulkan*)Shader)->ShaderStages.data();

    // Fixed-function stage
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &((jRasterizationStateInfo_Vulkan*)PipelineStateFixed->RasterizationState)->RasterizationStateInfo;
    pipelineInfo.pMultisampleState = &((jMultisampleStateInfo_Vulkan*)PipelineStateFixed->MultisampleState)->MultisampleStateInfo;
    pipelineInfo.pDepthStencilState = &((jDepthStencilStateInfo_Vulkan*)PipelineStateFixed->DepthStencilState)->DepthStencilStateInfo;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;			// Optional
    pipelineInfo.layout = vkPipelineLayout;
    pipelineInfo.renderPass = (VkRenderPass)RenderPass->GetRenderPass();
    pipelineInfo.subpass = 0;		// index of subpass

    // Pipeline을 상속받을 수 있는데, 아래와 같은 장점이 있다.
    // 1). 공통된 내용이 많으면 파이프라인 설정이 저렴하다.
    // 2). 공통된 부모를 가진 파이프라인 들끼리의 전환이 더 빠를 수 있다.
    // BasePipelineHandle 이나 BasePipelineIndex 가 로 Pipeline 내용을 상속받을 수 있다.
    // 이 기능을 사용하려면 VkGraphicsPipelineCreateInfo의 flags 에 VK_PIPELINE_CREATE_DERIVATIVE_BIT  가 설정되어있어야 함
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;		// Optional
    pipelineInfo.basePipelineIndex = -1;					// Optional

    // 여기서 두번째 파라메터 VkPipelineCache에 VK_NULL_HANDLE을 넘겼는데, VkPipelineCache는 VkPipeline을 저장하고 생성하는데 재사용할 수 있음.
    // 또한 파일로드 저장할 수 있어서 다른 프로그램에서 다시 사용할 수도있다. VkPipelineCache를 사용하면 VkPipeline을 생성하는 시간을 
    // 굉장히 빠르게 할수있다. (듣기로는 대략 1/10 의 속도로 생성해낼 수 있다고 함)
    if (!ensure(vkCreateGraphicsPipelines(g_rhi_vk->Device, g_rhi_vk->PipelineCache, 1, &pipelineInfo, nullptr, &vkPipeline) == VK_SUCCESS))
    {
        return nullptr;
    }

    return vkPipeline;
}

void jPipelineStateInfo_Vulkan::Bind() const
{
    check(vkPipeline);
    vkCmdBindPipeline((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
}
