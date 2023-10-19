#include "pch.h"
#include "jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "RHI/Vulkan/jVulkanBufferUtil.h"
#include "jOptions.h"
#include "RHI/jRenderFrameContext.h"
#include "Renderer/jSceneRenderTargets.h"
#include "RHI/jPipelineStateInfo.h"
#include "RHI/jRHI.h"
#include "imgui_internal.h"

jImGUI_Vulkan* jImGUI_Vulkan::s_instance = nullptr;

jImGUI_Vulkan::jImGUI_Vulkan()
{
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
    verify(VK_SUCCESS == vkCreateDescriptorPool(g_rhi_vk->Device, &descriptorPoolInfo, nullptr, &DescriptorPool));

    // Descriptor set layout
    {
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
            , ResourceInlineAllactor.Alloc<jTextureResource>(nullptr, nullptr));

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

    jTexture_Vulkan* FontImage_vk = new jTexture_Vulkan();
    FontImage = FontImage_vk;

    // Create target image for copy
    verify(jVulkanBufferUtil::CreateImage(texWidth, texHeight, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL
        , VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, *FontImage_vk));

    // Image view
    FontImage_vk->View = jVulkanBufferUtil::CreateImageView(FontImage_vk->Image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    check(FontImage_vk->View);

    jSamplerStateInfo* samplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();
    check(samplerStateInfo);

    VkDescriptorImageInfo fontDescriptor{};
    fontDescriptor.sampler = ((jSamplerStateInfo_Vulkan*)samplerStateInfo)->SamplerState;
    fontDescriptor.imageView = FontImage_vk->View;
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
    jBuffer_Vulkan stagingBuffer;
    verify(jVulkanBufferUtil::AllocateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
        , uploadSize, stagingBuffer));

    stagingBuffer.UpdateBuffer(fontData, uploadSize);

    // Copy buffer data to font image
    g_rhi_vk->TransitionImageLayoutImmediate(FontImage, EImageLayout::TRANSFER_DST);
    jVulkanBufferUtil::CopyBufferToImage(stagingBuffer.Buffer, stagingBuffer.Offset, FontImage_vk->Image, texWidth, texHeight);
    g_rhi_vk->TransitionImageLayoutImmediate(FontImage, EImageLayout::SHADER_READ_ONLY);

    stagingBuffer.Release();
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

        EmptyVertexBuffer = g_rhi->CreateVertexBuffer(vertexStreamData);
    }

    IsInitialized = true;
}

void jImGUI_Vulkan::Release()
{
    delete FontImage;
    FontImage = nullptr;

    DynamicBufferData.clear();

    DescriptorSet = nullptr;        // DescriptorPool 을 해제 하면되므로 따로 소멸시키지 않음
    if (DescriptorPool)
    {
        vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
        DescriptorPool = nullptr;
    }
 
    PushConstBlockPtr.reset();

    delete EmptyVertexBuffer;
    EmptyVertexBuffer = nullptr;
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
    VertexBufferArray.Add(EmptyVertexBuffer);

    jShaderBindingsLayoutArray ShaderBindingsLayoutArray;
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

void jImGUI_Vulkan::NewFrame(bool updateFrameGraph)
{
    if (!IsInitialized)
        return;

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Init imGui windows and elements

    ImVec4 clear_color = ImColor(114, 144, 154);
    static float f = 0.0f;
    //ImGui::TextUnformatted("TitleTest");
    //ImGui::TextUnformatted(g_rhi_vk->DeviceProperties.deviceName);

    ImGui::SetNextWindowSize(ImVec2(200.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Panel");

#if USE_VARIABLE_SHADING_RATE_TIER2
    ImGui::Checkbox("UseVRS", &gOptions.UseVRS);
#endif
    //ImGui::Checkbox("ShowVRSArea", &gOptions.ShowVRSArea);
    //ImGui::Checkbox("ShowGrid", &gOptions.ShowGrid);
    //ImGui::Checkbox("UseWaveIntrinsics", &gOptions.UseWaveIntrinsics);
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
        ImGui::Checkbox("[ReadOnly]UseSubpass", &gOptions.UseSubpass);
        ImGui::Checkbox("[ReadOnly]UseMemoryless", &gOptions.UseMemoryless);
        ImGui::PopItemFlag();
    }
    ImGui::Checkbox("ShowDebugObject", &gOptions.ShowDebugObject);
    ImGui::Checkbox("BloomEyeAdaptation", &gOptions.BloomEyeAdaptation);
    ImGui::Checkbox("QueueSubmitAfterShadowPass", &gOptions.QueueSubmitAfterShadowPass);
    ImGui::Checkbox("QueueSubmitAfterBasePass", &gOptions.QueueSubmitAfterBasePass);
    ImGui::SliderFloat("AutoExposureKeyValueScale", &gOptions.AutoExposureKeyValueScale, -12.0f, 12.0f);
    ImGui::Separator();
    ImGui::Text("PBR properties");
    ImGui::SliderFloat("Metallic", &gOptions.Metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &gOptions.Roughness, 0.0f, 1.0f);
    ImGui::Separator();

    constexpr float IndentSpace = 10.0f;
    const std::thread::id CurrentThreadId = std::this_thread::get_id();
    const ImVec4 OtherThreadColor = { 0.2f, 0.6f, 0.2f, 1.0f };
    {
        ImGui::Text("[CPU]");
        const auto& CPUAvgProfileMap = jPerformanceProfile::GetInstance().GetCPUAvgProfileMap();
        double TotalPassesMS = 0.0;
        int32 MostLeastIndent = INT_MAX;
        for (auto& pair : CPUAvgProfileMap)
        {
            const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
            const float Indent = IndentSpace * (float)AvgProfile.Indent;
            if (Indent > 0)
                ImGui::Indent(Indent);

            if (CurrentThreadId == AvgProfile.ThreadId)
                ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
            else
                ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);
            
            if (Indent > 0)
                ImGui::Unindent(Indent);
            
            // 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
            // 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
            if (CurrentThreadId == AvgProfile.ThreadId)
            {
                if (MostLeastIndent > AvgProfile.Indent)
                {
                    MostLeastIndent = AvgProfile.Indent;
                    TotalPassesMS = AvgProfile.AvgElapsedMS;
                }
                else if (MostLeastIndent == AvgProfile.Indent)
                {
                    TotalPassesMS += AvgProfile.AvgElapsedMS;
                }
            }
        }
        ImGui::Text("[CPU]Total Passes : %lf ms", TotalPassesMS);
    }
    ImGui::Separator();
    {
        ImGui::Text("[GPU]");
        const auto& GPUAvgProfileMap = jPerformanceProfile::GetInstance().GetGPUAvgProfileMap();
        double TotalPassesMS = 0.0;
        int32 MostLeastIndent = INT_MAX;
        for (auto& pair : GPUAvgProfileMap)
        {
            const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
            const float Indent = IndentSpace * (float)AvgProfile.Indent;
            if (Indent > 0)
                ImGui::Indent(Indent);

            if (CurrentThreadId == AvgProfile.ThreadId)
                ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
            else
                ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);
            
            if (Indent > 0)
                ImGui::Unindent(Indent);

            // 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
            // 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
            if (CurrentThreadId == AvgProfile.ThreadId)
            {
                if (MostLeastIndent > AvgProfile.Indent)
                {
                    MostLeastIndent = AvgProfile.Indent;
                    TotalPassesMS = AvgProfile.AvgElapsedMS;
                }
                else if (MostLeastIndent == AvgProfile.Indent)
                {
                    TotalPassesMS += AvgProfile.AvgElapsedMS;
                }
            }
        }
        ImGui::Text("[GPU]Total Passes : %lf ms", TotalPassesMS);
    }
    ImGui::Separator();
    for (auto& pair : CounterMap)
    {
        ImGui::Text("%s : %lld", pair.first.ToStr(), pair.second);
    }
    ImGui::Separator();
    ImGui::Text("CameraPos : %.2f, %.2f, %.2f", gOptions.CameraPos.x, gOptions.CameraPos.y, gOptions.CameraPos.z);
    ImGui::End();

    //// Update frame time display
    //if (updateFrameGraph) {
    //    std::rotate(uiSettings.frameTimes.begin(), uiSettings.frameTimes.begin() + 1, uiSettings.frameTimes.end());
    //    float frameTime = 1000.0f / (example->frameTimer * 1000.0f);
    //    uiSettings.frameTimes.back() = frameTime;
    //    if (frameTime < uiSettings.frameTimeMin) {
    //        uiSettings.frameTimeMin = frameTime;
    //    }
    //    if (frameTime > uiSettings.frameTimeMax) {
    //        uiSettings.frameTimeMax = frameTime;
    //    }
    //}

    //ImGui::PlotLines("Frame Times", &uiSettings.frameTimes[0], 50, 0, "", uiSettings.frameTimeMin, uiSettings.frameTimeMax, ImVec2(0, 80));

    //ImGui::Text("Camera");
    //ImGui::InputFloat3("position", &example->camera.position.x, 2);
    //ImGui::InputFloat3("rotation", &example->camera.rotation.x, 2);

    //ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiSetCond_FirstUseEver);
    //ImGui::Begin("Example settings");
    //ImGui::Checkbox("Render models", &uiSettings.displayModels);
    //ImGui::Checkbox("Display logos", &uiSettings.displayLogos);
    //ImGui::Checkbox("Display background", &uiSettings.displayBackground);
    //ImGui::Checkbox("Animate light", &uiSettings.animateLight);
    //ImGui::SliderFloat("Light speed", &uiSettings.lightSpeed, 0.1f, 1.0f);
    //ImGui::End();

    //ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiSetCond_FirstUseEver);
    //ImGui::ShowDemoWindow();

    // Render to generate draw buffers
    ImGui::Render();
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
    if ((DynamicBuffer.VertexBufferPtr->Buffer == VK_NULL_HANDLE) || (DynamicBuffer.vertexCount != imDrawData->TotalVtxCount))
    {
        const uint64 newBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);

        DynamicBuffer.VertexBufferPtr->Release();
        verify(jVulkanBufferUtil::AllocateBuffer(EVulkanBufferBits::VERTEX_BUFFER, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
            , newBufferSize, *DynamicBuffer.VertexBufferPtr.get()));
        DynamicBuffer.VertexBufferPtr->Map();
        DynamicBuffer.vertexCount = imDrawData->TotalVtxCount;
    }

    // Index buffer
    if ((DynamicBuffer.IndexBufferPtr->Buffer == VK_NULL_HANDLE) || (DynamicBuffer.indexCount < imDrawData->TotalIdxCount))
    {
        const uint64 newBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

        DynamicBuffer.IndexBufferPtr->Release();
        verify(jVulkanBufferUtil::AllocateBuffer(EVulkanBufferBits::INDEX_BUFFER, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
            , newBufferSize, *DynamicBuffer.IndexBufferPtr.get()));
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

    NewFrame((g_rhi_vk->CurrentFrameIndex == 0));
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

        jAttachment color = jAttachment(FinalColorPtr, EAttachmentLoadStoreOp::LOAD_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f)
            , FinalColorPtr->GetLayout(), EImageLayout::PRESENT_SRC);

        RenderPass = g_rhi_vk->GetOrCreateRenderPass({ color }, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
        PiplineStateInfo = (jPipelineStateInfo_Vulkan*)jImGUI_Vulkan::Get().CreatePipelineState(RenderPass, g_rhi_vk->GraphicsQueue.Queue);
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

        VkViewport viewport = {};
        viewport.width = ImGui::GetIO().DisplaySize.x;
        viewport.height = ImGui::GetIO().DisplaySize.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandbuffer_vk, 0, 1, &viewport);

        // UI scale and translate via push constants
        pushConstBlock.scale = Vector2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
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

