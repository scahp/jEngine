#include "pch.h"
#include "jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "RHI/Vulkan/jVulkanBufferUtil.h"

jImGUI_Vulkan* jImGUI_Vulkan::s_instance = nullptr;

jImGUI_Vulkan::jImGUI_Vulkan()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(g_rhi_vk->Window, true);
}

jImGUI_Vulkan::~jImGUI_Vulkan()
{
    ImGui::DestroyContext();
    // Release all Vulkan resources required for rendering imGui
    for (int32 i = 0; i < DynamicBufferData.size(); ++i)
    {
        DynamicBufferData[i].VertexBuffer.Destroy();
        DynamicBufferData[i].IndexBuffer.Destroy();
    }
    if (FontImage)
        delete FontImage;
    vkDestroyPipelineLayout(g_rhi_vk->Device, PipelineLayout, nullptr);
    vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(g_rhi_vk->Device, DescriptorSetLayout, nullptr);
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
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
    setLayoutBindings.resize(1);
    setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    setLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayoutBindings[0].binding = 0;
    setLayoutBindings[0].descriptorCount = 1;

    VkDescriptorSetLayoutCreateInfo descriptorLayout{};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pBindings = setLayoutBindings.data();
    descriptorLayout.bindingCount = static_cast<uint32>(setLayoutBindings.size());
    verify(VK_SUCCESS == vkCreateDescriptorSetLayout(g_rhi_vk->Device, &descriptorLayout, nullptr, &DescriptorSetLayout));

    // Descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.pSetLayouts = &DescriptorSetLayout;
    allocInfo.descriptorSetCount = 1;
    verify(VK_SUCCESS == vkAllocateDescriptorSets(g_rhi_vk->Device, &allocInfo, &DescriptorSet));
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

    verify(jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        , uploadSize, stagingBuffer));

    stagingBuffer.UpdateBuffer(fontData, uploadSize);

    // Copy buffer data to font image
    g_rhi_vk->TransitionImageLayoutImmediate(FontImage, EImageLayout::TRANSFER_DST);
    jVulkanBufferUtil::CopyBufferToImage(stagingBuffer.Buffer, FontImage_vk->Image, texWidth, texHeight);
    g_rhi_vk->TransitionImageLayoutImmediate(FontImage, EImageLayout::SHADER_READ_ONLY);

    stagingBuffer.Destroy();
    //////////////////////////////////////////////////////////////////////////
}

VkPipeline jImGUI_Vulkan::CreatePipelineState(VkRenderPass renderPass, VkQueue copyQueue)
{
    // Pipeline layout
    // Push constants for UI rendering parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstBlock);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &DescriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    verify(VK_SUCCESS == vkCreatePipelineLayout(g_rhi_vk->Device, &pipelineLayoutCreateInfo, nullptr, &PipelineLayout));

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

    jMultisampleStateInfo* multisamplesStateInfo = TMultisampleStateInfo<>::Create(EMSAASamples::COUNT_1);
    check(multisamplesStateInfo);

    // Setup graphics pipeline for UI rendering
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyState.flags = 0;
    inputAssemblyState.primitiveRestartEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.attachmentCount = 1;
    colorBlendState.pAttachments = &((jBlendingStateInfo_Vulakn*)blendStateInfo)->ColorBlendAttachmentInfo;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.flags = 0;

    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pDynamicStates = dynamicStateEnables.data();
    dynamicState.dynamicStateCount = static_cast<uint32>(dynamicStateEnables.size());
    dynamicState.flags = 0;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.layout = PipelineLayout;
    pipelineCreateInfo.renderPass = renderPass;
    pipelineCreateInfo.flags = 0;
    pipelineCreateInfo.basePipelineIndex = -1;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;

    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &((jRasterizationStateInfo_Vulkan*)rasterizationInfo)->RasterizationStateInfo;
    pipelineCreateInfo.pMultisampleState = &((jMultisampleStateInfo_Vulkan*)multisamplesStateInfo)->MultisampleStateInfo;
    pipelineCreateInfo.pDepthStencilState = &((jDepthStencilStateInfo_Vulkan*)depthStencilInfo)->DepthStencilStateInfo;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    pipelineCreateInfo.stageCount = static_cast<uint32>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();

    // Vertex bindings an attributes based on ImGui vertex definition
    std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
        VkVertexInputBindingDescription(0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX),
    };
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
        VkVertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)),	// Location 0: Position
        VkVertexInputAttributeDescription(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)),	// Location 1: UV
        VkVertexInputAttributeDescription(2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)),	// Location 0: Color
    };
    VkPipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputState.vertexBindingDescriptionCount = static_cast<uint32>(vertexInputBindings.size());
    vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
    vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32>(vertexInputAttributes.size());
    vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

    pipelineCreateInfo.pVertexInputState = &vertexInputState;

    auto LoadShaderFromSRV = [](const char* fileName) -> VkShaderModule
    {
        std::ifstream is(fileName, std::ios::binary | std::ios::in | std::ios::ate);

        if (is.is_open())
        {
            size_t size = is.tellg();
            is.seekg(0, std::ios::beg);
            char* shaderCode = new char[size];
            is.read(shaderCode, size);
            is.close();

            assert(size > 0);

            VkShaderModule shaderModule = nullptr;
            VkShaderModuleCreateInfo moduleCreateInfo{};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = size;
            moduleCreateInfo.pCode = (uint32_t*)shaderCode;

            verify(VK_SUCCESS == vkCreateShaderModule(g_rhi_vk->Device, &moduleCreateInfo, NULL, &shaderModule));
            delete[] shaderCode;

            return shaderModule;
        }
        return nullptr;
    };

    VkPipelineShaderStageCreateInfo vertexShaderStage = {};
    vertexShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStage.module = LoadShaderFromSRV("External/IMGUI/ui.vert.spv");
    vertexShaderStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentShaderStage = {};
    fragmentShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentShaderStage.module = LoadShaderFromSRV("External/IMGUI/ui.frag.spv");
    fragmentShaderStage.pName = "main";

    shaderStages[0] = vertexShaderStage;
    shaderStages[1] = fragmentShaderStage;

    VkPipeline pipeline = nullptr;
    verify(VK_SUCCESS == vkCreateGraphicsPipelines(g_rhi_vk->Device, g_rhi_vk->PipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));

    vkDestroyShaderModule(g_rhi_vk->Device, vertexShaderStage.module, nullptr);
    vkDestroyShaderModule(g_rhi_vk->Device, fragmentShaderStage.module, nullptr);

    return pipeline;
}

void jImGUI_Vulkan::Update(float InDeltaSeconds)
{
    jImGUI_Vulkan::Get().NewFrame((g_rhi_vk->CurrenFrameIndex == 0));
    jImGUI_Vulkan::Get().UpdateBuffers();
}

void jImGUI_Vulkan::NewFrame(bool updateFrameGraph)
{
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Init imGui windows and elements

    ImVec4 clear_color = ImColor(114, 144, 154);
    static float f = 0.0f;
    //ImGui::TextUnformatted("TitleTest");
    //ImGui::TextUnformatted(g_rhi_vk->DeviceProperties.deviceName);

    ImGui::SetNextWindowSize(ImVec2(200.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("GPU Time Stamp");
    const std::map<jName, jPerformanceProfile::jAvgProfile>& GPUAvgProfileMap = jPerformanceProfile::GetInstance().GetGPUAvgProfileMap();
    for (auto& pair : GPUAvgProfileMap)
    {
        ImGui::Text("%s : %lf ms", pair.first.ToStr(), pair.second.AvgElapsedMS);
    }
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
    ImDrawData* imDrawData = ImGui::GetDrawData();
    if ((imDrawData->TotalVtxCount == 0) || (imDrawData->TotalIdxCount == 0))
        return;

    if (DynamicBufferData.size() <= g_rhi_vk->CurrenFrameIndex)
        DynamicBufferData.resize(g_rhi_vk->CurrenFrameIndex + 1);

    auto& DynamicBuffer = DynamicBufferData[g_rhi_vk->CurrenFrameIndex];

    // Update buffers only if vertex or index count has been changed compared to current buffer size

    // Vertex buffer
    if ((DynamicBuffer.VertexBuffer.Buffer == VK_NULL_HANDLE) || (DynamicBuffer.vertexCount != imDrawData->TotalVtxCount))
    {
        const uint64 newBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);

        DynamicBuffer.VertexBuffer.Destroy();
        verify(jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            , newBufferSize, DynamicBuffer.VertexBuffer));
        DynamicBuffer.vertexCount = imDrawData->TotalVtxCount;
        DynamicBuffer.VertexBuffer.Map();
    }

    // Index buffer
    if ((DynamicBuffer.IndexBuffer.Buffer == VK_NULL_HANDLE) || (DynamicBuffer.indexCount < imDrawData->TotalIdxCount))
    {
        const uint64 newBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

        DynamicBuffer.IndexBuffer.Destroy();
        verify(jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            , newBufferSize, DynamicBuffer.IndexBuffer));
        DynamicBuffer.indexCount = imDrawData->TotalIdxCount;
        DynamicBuffer.IndexBuffer.Map();
    }

    // Upload data
    ImDrawVert* vtxDst = (ImDrawVert*)DynamicBuffer.VertexBuffer.GetMappedPointer();
    ImDrawIdx* idxDst = (ImDrawIdx*)DynamicBuffer.IndexBuffer.GetMappedPointer();

    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
        const ImDrawList* cmd_list = imDrawData->CmdLists[n];
        memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmd_list->VtxBuffer.Size;
        idxDst += cmd_list->IdxBuffer.Size;
    }

    // Flush to make writes visible to GPU
    {
        VkMappedMemoryRange mappedRange = {};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = DynamicBuffer.VertexBuffer.BufferMemory;
        mappedRange.offset = 0;
        mappedRange.size = DynamicBuffer.VertexBuffer.AllocatedSize;
        verify(VK_SUCCESS == vkFlushMappedMemoryRanges(g_rhi_vk->Device, 1, &mappedRange));
    }
    {
        VkMappedMemoryRange mappedRange = {};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = DynamicBuffer.IndexBuffer.BufferMemory;
        mappedRange.offset = 0;
        mappedRange.size = DynamicBuffer.IndexBuffer.AllocatedSize;
        verify(VK_SUCCESS == vkFlushMappedMemoryRanges(g_rhi_vk->Device, 1, &mappedRange));
    }
}

void jImGUI_Vulkan::PrepareDraw(int32 imageIndex)
{
    check(RenderPasses.size() == Pipelines.size());

    if (RenderPasses.size() <= imageIndex)
    {
        RenderPasses.resize(imageIndex + 1);
        Pipelines.resize(imageIndex + 1);

        const auto& extent = g_rhi_vk->Swapchain->GetExtent();
        const auto& image = g_rhi_vk->Swapchain->GetSwapchainImage(imageIndex);

        auto SwapChainRTPtr = jRenderTarget::CreateFromTexture(image->TexturePtr);

        jAttachment color = jAttachment(SwapChainRTPtr, EAttachmentLoadStoreOp::LOAD_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, Vector4(0.0f, 0.0f, 0.0f, 1.0f), Vector2(1.0f, 0.0f)
            , EImageLayout::GENERAL, EImageLayout::PRESENT_SRC);

        auto newRenderPass = g_rhi_vk->GetOrCreateRenderPass({ color }, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
        RenderPasses[imageIndex] = newRenderPass;
        Pipelines[imageIndex] = jImGUI_Vulkan::Get().CreatePipelineState((VkRenderPass)RenderPasses[imageIndex]->GetRenderPass(), g_rhi_vk->GraphicsQueue.Queue);
    }
}

void jImGUI_Vulkan::Draw(jCommandBuffer* commandBuffer, int32 imageIndex)
{
    PrepareDraw(imageIndex);

    if (RenderPasses[imageIndex]->BeginRenderPass(commandBuffer))
    {
        VkCommandBuffer commandbuffer_vk = (VkCommandBuffer)commandBuffer->GetHandle();

        ImGuiIO& io = ImGui::GetIO();

        vkCmdBindDescriptorSets(commandbuffer_vk, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
        vkCmdBindPipeline(commandbuffer_vk, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines[imageIndex]);

        VkViewport viewport = {};
        viewport.width = ImGui::GetIO().DisplaySize.x;
        viewport.height = ImGui::GetIO().DisplaySize.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandbuffer_vk, 0, 1, &viewport);

        // UI scale and translate via push constants
        pushConstBlock.scale = Vector2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
        pushConstBlock.translate = Vector2(-1.0f);
        vkCmdPushConstants(commandbuffer_vk, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);

        // Render commands
        ImDrawData* imDrawData = ImGui::GetDrawData();
        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;

        if (imDrawData->CmdListsCount > 0 && DynamicBufferData.size() > imageIndex)
        {
            auto& DynamicBuffer = DynamicBufferData[imageIndex];

            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(commandbuffer_vk, 0, 1, &DynamicBuffer.VertexBuffer.Buffer, offsets);
            vkCmdBindIndexBuffer(commandbuffer_vk, DynamicBuffer.IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT16);

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

        RenderPasses[imageIndex]->EndRenderPass();
    }
}

