#include "pch.h"
#include "jPipelineStateInfo.h"
#include "Vulkan/jVertexBuffer_Vulkan.h"

VkPipeline jPipelineStateInfo::CreateGraphicsPipelineState()
{
    // 미리 만들어 둔게 있으면 사용
    if (vkPipeline)
        return vkPipeline;

    check(PipelineStateFixed);

    Hash = GetHash();
    check(Hash);
    {
        auto it_find = PipelineStatePool.find(Hash);
        if (PipelineStatePool.end() != it_find)
        {
            *this = it_find->second;
            return vkPipeline;
        }
    }

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
    if (!ensure(vkCreateGraphicsPipelines(g_rhi_vk->device, g_rhi_vk->PipelineCache, 1, &pipelineInfo, nullptr, &vkPipeline) == VK_SUCCESS))
    {
        return nullptr;
    }

    PipelineStatePool.insert(std::make_pair(Hash, *this));

    return vkPipeline;
}


void jPipelineStateInfo::Bind()
{
    check(vkPipeline);
    vkCmdBindPipeline((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
}