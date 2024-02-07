#include "pch.h"
#include "jImGui_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "RHI/Vulkan/jBufferUtil_Vulkan.h"
#include "jOptions.h"
#include "RHI/jRenderFrameContext.h"
#include "Renderer/jSceneRenderTargets.h"
#include "RHI/jPipelineStateInfo.h"
#include "RHI/jRHI.h"
#include "imgui_internal.h"

jImGUI_Vulkan::jImGUI_Vulkan()
{
    check(g_rhi_vk);
    check(g_rhi_vk->Window);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(g_rhi_vk->Window, true);
}

jImGUI_Vulkan::~jImGUI_Vulkan()
{
    Release();
    ImGui::DestroyContext();
}

void jImGUI_Vulkan::Initialize(float width, float height)
{
	// Handle Monitor DPI
	float MonitorDPIScaleX = 0;
	float MonitorDPIScaleY = 0;
	GLFWmonitor* PrimaryMonitor = glfwGetPrimaryMonitor();
	glfwGetMonitorContentScale(PrimaryMonitor, &MonitorDPIScaleX, &MonitorDPIScaleY);
	MonitorDPIScale = Max(MonitorDPIScaleX, MonitorDPIScaleY);

	ImGui::GetStyle().ScaleAllSizes(MonitorDPIScale);
	ImGui::GetIO().FontGlobalScale = MonitorDPIScale;
	//////////////////////////////////////////////////////////////////////////

    // Color scheme
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    // Dimensions
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    
    //////////////////////////////////////////////////////////////////////////
    // Descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.resize(1);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 2;
    descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    verify(VK_SUCCESS == vkCreateDescriptorPool(g_rhi_vk->Device, &descriptorPoolInfo, nullptr, &DescriptorPool));

    // Descriptor set layout
    {
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
            , ResourceInlineAllactor.Alloc<jTextureResource>(nullptr, nullptr)));

        EmptyShaderBindingLayout = g_rhi->CreateShaderBindings(ShaderBindingArray);
    }

    VkDescriptorSetLayout DescriptorSetLayout = (VkDescriptorSetLayout)EmptyShaderBindingLayout->GetHandle();

    // Descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.pSetLayouts = &DescriptorSetLayout;
    allocInfo.descriptorSetCount = 1;
    verify(VK_SUCCESS == vkAllocateDescriptorSets(g_rhi_vk->Device, &allocInfo, &DescriptorSet));

    PushConstBlockPtr = std::make_shared<jPushConstant>(PushConstBlock(), EShaderAccessStageFlag::VERTEX);
    //////////////////////////////////////////////////////////////////////////
    // 
    //////////////////////////////////////////////////////////////////////////
    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    VkDeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);

    // Create target image for copy
    FontImagePtr = g_rhi->Create2DTexture<jTexture_Vulkan>(texWidth, texHeight, 1, 1, ETextureFormat::RGBA8, ETextureCreateFlag::TransferDst);
    check(FontImagePtr);

    jSamplerStateInfo* samplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();
    check(samplerStateInfo);

    VkDescriptorImageInfo fontDescriptor{};
    fontDescriptor.sampler = ((jSamplerStateInfo_Vulkan*)samplerStateInfo)->SamplerState;
    fontDescriptor.imageView = FontImagePtr->View;
    fontDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets;
    writeDescriptorSets.resize(1);
    writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSets[0].dstSet = DescriptorSet;
    writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSets[0].dstBinding = 0;
    writeDescriptorSets[0].pImageInfo = &fontDescriptor;
    writeDescriptorSets[0].descriptorCount = 1;
    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    //////////////////////////////////////////////////////////////////////////
    // 
    //////////////////////////////////////////////////////////////////////////
    // Staging buffers for font data upload
    std::shared_ptr<jBuffer_Vulkan> stagingBuffer = jBufferUtil_Vulkan::CreateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT, uploadSize, EResourceLayout::TRANSFER_SRC);
    stagingBuffer->UpdateBuffer(fontData, uploadSize);

    // Copy buffer data to font image
    g_rhi_vk->TransitionLayout(FontImagePtr.get(), EResourceLayout::TRANSFER_DST);
    jBufferUtil_Vulkan::CopyBufferToTexture(stagingBuffer->Buffer, stagingBuffer->Offset, FontImagePtr->Image, texWidth, texHeight);
    g_rhi_vk->TransitionLayout(FontImagePtr.get(), EResourceLayout::SHADER_READ_ONLY);
    //////////////////////////////////////////////////////////////////////////

    DynamicBufferData.resize(g_rhi_vk->Swapchain->GetNumOfSwapchain());
    for (auto& iter : DynamicBufferData)
    {
        iter.Initialize();
    }

    // CreateEmptyVertexBuffer to store vertex stream type
    {
        auto vertexStreamData = std::make_shared<jVertexStreamData>();
        {
            auto streamParam = std::make_shared<jStreamParam<float>>();
            streamParam->BufferType = EBufferType::STATIC;
            streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
            streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
            streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::BYTE_UNORM, sizeof(unsigned char) * 4));
            streamParam->Stride = (sizeof(float) * 2) + (sizeof(float) * 2) + (sizeof(unsigned char) * 4);
            streamParam->Name = jName("Position_UV_Color");
            vertexStreamData->Params.push_back(streamParam);
        }

        vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
        vertexStreamData->ElementCount = 0;

        EmptyVertexBufferPtr = g_rhi->CreateVertexBuffer(vertexStreamData);
    }

    IsInitialized = true;
}

void jImGUI_Vulkan::Release()
{
    DynamicBufferData.clear();

    DescriptorSet = nullptr;        // DescriptorPool 을 해제 하면되므로 따로 소멸시키지 않음
    if (DescriptorPool)
    {
        vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
        DescriptorPool = nullptr;
    }
 
    PushConstBlockPtr.reset();
}

jPipelineStateInfo* jImGUI_Vulkan::CreatePipelineState(jRenderPass* renderPass, VkQueue copyQueue)
{
    jRasterizationStateInfo* rasterizationInfo = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE, EFrontFace::CCW>::Create();
    check(rasterizationInfo);

    // Enable blending
    jBlendingStateInfo* blendStateInfo = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD
        , EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    check(blendStateInfo);

    jStencilOpStateInfo* stencilStateBack = TStencilOpStateInfo<EStencilOp::KEEP, EStencilOp::KEEP, EStencilOp::KEEP, ECompareOp::ALWAYS>::Create();
    check(stencilStateBack);

    jDepthStencilStateInfo* depthStencilInfo = TDepthStencilStateInfo<>::Create(nullptr, stencilStateBack);
    check(depthStencilInfo);

    //jMultisampleStateInfo* multisamplesStateInfo = TMultisampleStateInfo<>::Create(EMSAASamples::COUNT_1);
    //check(multisamplesStateInfo);

    static std::vector<EPipelineDynamicState> PipelineDynamicStates = { EPipelineDynamicState::VIEWPORT, EPipelineDynamicState::SCISSOR };
    jPipelineStateFixedInfo PipelineStateFixed(rasterizationInfo, depthStencilInfo, blendStateInfo, PipelineDynamicStates, false);

    jVertexBufferArray VertexBufferArray;
    VertexBufferArray.Add(EmptyVertexBufferPtr.get());

    jShaderBindingLayoutArray ShaderBindingsLayoutArray;
    ShaderBindingsLayoutArray.Add(EmptyShaderBindingLayout);

    jGraphicsPipelineShader Shader;
    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("imgui_VS"));
        shaderInfo.SetShaderFilepath(jNameStatic("External/IMGUI/ui.vert.spv"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        Shader.VertexShader = g_rhi->CreateShader(shaderInfo);
    }
    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("imgui_PS"));
        shaderInfo.SetShaderFilepath(jNameStatic("External/IMGUI/ui.frag.spv"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
        Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
    }

    return g_rhi->CreatePipelineStateInfo(&PipelineStateFixed, Shader
        , VertexBufferArray, renderPass, ShaderBindingsLayoutArray, PushConstBlockPtr.get(), 0);
}

void jImGUI_Vulkan::NewFrameInternal()
{
    if (!IsInitialized)
        return;

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void jImGUI_Vulkan::UpdateBuffers()
{
    if (!IsInitialized)
        return;

    ImDrawData* imDrawData = ImGui::GetDrawData();
    if ((imDrawData->TotalVtxCount == 0) || (imDrawData->TotalIdxCount == 0))
        return;

    auto& DynamicBuffer = DynamicBufferData[g_rhi_vk->CurrentFrameIndex];

    // Update buffers only if vertex or index count has been changed compared to current buffer size

    // Vertex buffer
    if (!DynamicBuffer.VertexBufferPtr || (DynamicBuffer.VertexBufferPtr->Buffer == VK_NULL_HANDLE) || (DynamicBuffer.vertexCount != imDrawData->TotalVtxCount))
    {
        const uint64 newBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
        DynamicBuffer.VertexBufferPtr = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(newBufferSize, 0
            , EBufferCreateFlag::VertexBuffer | EBufferCreateFlag::CPUAccess, EResourceLayout::GENERAL);
        DynamicBuffer.VertexBufferPtr->Map();
        DynamicBuffer.vertexCount = imDrawData->TotalVtxCount;
    }

    // Index buffer
    if (!DynamicBuffer.IndexBufferPtr || (DynamicBuffer.IndexBufferPtr->Buffer == VK_NULL_HANDLE) || (DynamicBuffer.indexCount < imDrawData->TotalIdxCount))
    {
        const uint64 newBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

        DynamicBuffer.IndexBufferPtr = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(newBufferSize, 0
            , EBufferCreateFlag::IndexBuffer | EBufferCreateFlag::CPUAccess, EResourceLayout::GENERAL);
        DynamicBuffer.IndexBufferPtr->Map();
        DynamicBuffer.indexCount = imDrawData->TotalIdxCount;
    }

    // Upload data
    ImDrawVert* vtxDst = (ImDrawVert*)DynamicBuffer.VertexBufferPtr->GetMappedPointer();
    ImDrawIdx* idxDst = (ImDrawIdx*)DynamicBuffer.IndexBufferPtr->GetMappedPointer();

    for (int n = 0; n < imDrawData->CmdListsCount; n++) 
    {
        const ImDrawList* cmd_list = imDrawData->CmdLists[n];
        memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmd_list->VtxBuffer.Size;
        idxDst += cmd_list->IdxBuffer.Size;
    }
}

void jImGUI_Vulkan::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
    if (!IsInitialized)
        return;

    UpdateBuffers();

    DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "ImGUI", Vector4(0.8f, 0.8f, 0.8f, 1.0f));
    check(InRenderFrameContextPtr);

    // Prepare draw
    jRenderPass* RenderPass = nullptr;
    jPipelineStateInfo_Vulkan* PiplineStateInfo = nullptr;
    {
        const auto& extent = g_rhi_vk->Swapchain->GetExtent();
        const auto& image = g_rhi_vk->Swapchain->GetSwapchainImage(InRenderFrameContextPtr->FrameIndex);

        const auto& FinalColorPtr = InRenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr;
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), FinalColorPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);

        jAttachment color = jAttachment(FinalColorPtr, EAttachmentLoadStoreOp::LOAD_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f)
            , FinalColorPtr->GetLayout(), EResourceLayout::PRESENT_SRC);

        RenderPass = g_rhi_vk->GetOrCreateRenderPass({ color }, { 0, 0 }, { FinalColorPtr->Info.Width, FinalColorPtr->Info.Height });
        PiplineStateInfo = (jPipelineStateInfo_Vulkan*)CreatePipelineState(RenderPass, g_rhi_vk->GraphicsQueue.Queue);
    }
    check(RenderPass);
    check(PiplineStateInfo);

    const int32 frameIndex = InRenderFrameContextPtr->FrameIndex;
    if (RenderPass->BeginRenderPass(InRenderFrameContextPtr->GetActiveCommandBuffer()))
    {
        VkCommandBuffer commandbuffer_vk = (VkCommandBuffer)InRenderFrameContextPtr->GetActiveCommandBuffer()->GetHandle();

        ImGuiIO& io = ImGui::GetIO();

        const VkPipelineLayout pipelineLayout = PiplineStateInfo->vkPipelineLayout;

        vkCmdBindDescriptorSets(commandbuffer_vk, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
        vkCmdBindPipeline(commandbuffer_vk, VK_PIPELINE_BIND_POINT_GRAPHICS, PiplineStateInfo->vkPipeline);

        const float DisplayWidth = io.DisplaySize.x;
        const float DisplayHeight = io.DisplaySize.y;

        VkViewport viewport = {};
        viewport.width = DisplayWidth;
        viewport.height = DisplayHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandbuffer_vk, 0, 1, &viewport);

        // UI scale and translate via push constants
        pushConstBlock.scale = Vector2(2.0f / DisplayWidth, 2.0f / DisplayHeight);
        pushConstBlock.translate = Vector2(-1.0f);
        vkCmdPushConstants(commandbuffer_vk, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);

        // Render commands
        ImDrawData* imDrawData = ImGui::GetDrawData();
        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;

        if (imDrawData->CmdListsCount > 0 && DynamicBufferData.size() > frameIndex)
        {
            auto& DynamicBuffer = DynamicBufferData[frameIndex];

            if (DynamicBuffer.VertexBufferPtr->Buffer && DynamicBuffer.IndexBufferPtr->Buffer)
            {
                VkDeviceSize offsets[1] = { 0 };
                vkCmdBindVertexBuffers(commandbuffer_vk, 0, 1, &DynamicBuffer.VertexBufferPtr->Buffer, offsets);
                vkCmdBindIndexBuffer(commandbuffer_vk, DynamicBuffer.IndexBufferPtr->Buffer, 0, VK_INDEX_TYPE_UINT16);

                for (int32_t i = 0; i < imDrawData->CmdListsCount; i++)
                {
                    const ImDrawList* cmd_list = imDrawData->CmdLists[i];
                    for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
                    {
                        const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
                        VkRect2D scissorRect;
                        scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
                        scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
                        scissorRect.extent.width = (uint32)(pcmd->ClipRect.z - pcmd->ClipRect.x);
                        scissorRect.extent.height = (uint32)(pcmd->ClipRect.w - pcmd->ClipRect.y);
                        vkCmdSetScissor(commandbuffer_vk, 0, 1, &scissorRect);
                        vkCmdDrawIndexed(commandbuffer_vk, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
                        indexOffset += pcmd->ElemCount;
                    }
                    vertexOffset += cmd_list->VtxBuffer.Size;
                }
            }
        }

        RenderPass->EndRenderPass();
    }
}

