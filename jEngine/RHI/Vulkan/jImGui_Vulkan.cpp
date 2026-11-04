#include "pch.h"
#include "jImGui_Vulkan.h"
#include "RHI/Vulkan/jRHI_Vulkan.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "RHI/jRHI.h"
#include "Renderer/jSceneRenderTargets.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

namespace
{
jRenderPass* UIRenderPass(const std::shared_ptr<jRenderTarget>& target)
{
    jAttachment color = {
        .RenderTargetPtr = target,
        .LoadStoreOp = EAttachmentLoadStoreOp::LOAD_STORE,
        .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        // Initialization creates a compatible pipeline pass before the first
        // swapchain transition. LOAD can never declare UNDEFINED, even when
        // this compatibility-only pass is not submitted.
        .InitialLayout = target->GetLayout() == EResourceLayout::UNDEFINED ? EResourceLayout::COLOR_ATTACHMENT : target->GetLayout(),
        .FinalLayout = EResourceLayout::PRESENT_SRC
    };
    return g_rhi->GetOrCreateRenderPass({ color }, {0, 0}, {target->Info.Width, target->Info.Height});
}
}

jImGUI_Vulkan::jImGUI_Vulkan()
{
    check(g_rhi_vk && g_rhi_vk->Window);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    const ImGuiConfigFlags saved = io.ConfigFlags;
    // Install real callbacks even when the user starts with detached windows off.
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    PlatformInitialized = ImGui_ImplGlfw_InitForVulkan(g_rhi_vk->Window, true);
    io.ConfigFlags = saved;
}
jImGUI_Vulkan::~jImGUI_Vulkan()
{
    Release();
    RegisteredTextures.clear();
    if (DescriptorPool) vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
    DescriptorPool = VK_NULL_HANDLE;
    if (PlatformInitialized) ImGui_ImplGlfw_Shutdown();
    PlatformInitialized = false;
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
}
void jImGUI_Vulkan::Initialize(float width, float height)
{
    if (IsInitialized) return;
    if (!PlatformInitialized)
    {
        auto& platformIO = ImGui::GetIO();
        const ImGuiConfigFlags saved = platformIO.ConfigFlags;
        platformIO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        PlatformInitialized = ImGui_ImplGlfw_InitForVulkan(g_rhi_vk->Window, true);
        platformIO.ConfigFlags = saved;
        if (!PlatformInitialized) return;
    }
    float x = 1, y = 1;
    if (auto* monitor = glfwGetPrimaryMonitor()) glfwGetMonitorContentScale(monitor, &x, &y);
    MonitorDPIScale = Max(x, y);
    if (!DescriptorPool)
    {
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(MonitorDPIScale);
    }
    auto& io = ImGui::GetIO();
    io.FontGlobalScale = MonitorDPIScale;
    io.DisplaySize = ImVec2(width, height);
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 4096; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
    if (!DescriptorPool && vkCreateDescriptorPool(g_rhi_vk->Device, &pool, nullptr, &DescriptorPool) != VK_SUCCESS) return;
    auto* image = g_rhi_vk->Swapchain->GetSwapchainImage(0);
    auto target = std::make_shared<jRenderTarget>(image->TexturePtr);
    auto* pass = UIRenderPass(target);
    if (!pass) return;
    ImGui_ImplVulkan_InitInfo init{};
    init.Instance = g_rhi_vk->Instance; init.PhysicalDevice = g_rhi_vk->PhysicalDevice;
    init.Device = g_rhi_vk->Device; init.QueueFamily = g_rhi_vk->GraphicsQueue.QueueIndex;
    init.Queue = g_rhi_vk->GraphicsQueue.Queue; init.PipelineCache = g_rhi_vk->PipelineCache;
    init.DescriptorPool = DescriptorPool;
    init.MinImageCount = 2; init.ImageCount = uint32(g_rhi_vk->Swapchain->GetNumOfSwapchain());
    init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.CheckVkResultFn = [](VkResult result) { if (result < VK_SUCCESS) std::cerr << "ImGui Vulkan error: " << result << '\n'; };
    const ImGuiConfigFlags saved = io.ConfigFlags;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    IsInitialized = ImGui_ImplVulkan_Init(&init, (VkRenderPass)pass->GetRenderPass());
    io.ConfigFlags = saved;
    if (!IsInitialized) return;
    auto* command = g_rhi_vk->BeginSingleTimeCommands();
    ImGui_ImplVulkan_CreateFontsTexture((VkCommandBuffer)command->GetHandle());
    g_rhi_vk->EndSingleTimeCommands(command);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}
void jImGUI_Vulkan::Release()
{
    // Swapchain recreation calls Release followed by Initialize on this same
    // context. Renderer shutdown destroys platform-window metadata too, so
    // Initialize must reinstall GLFW callbacks and main-viewport metadata.
    // Keep registered texture descriptor leases/pool alive until the destructor.
    if (IsInitialized)
    {
        ImGui_ImplVulkan_Shutdown();
        IsInitialized = false;
    }
    if (PlatformInitialized)
    {
        ImGui_ImplGlfw_Shutdown();
        PlatformInitialized = false;
    }
}
void* jImGUI_Vulkan::RegisterTexture(const std::shared_ptr<jTexture>& texture)
{
    if (!IsInitialized || !texture || !texture->GetHandle() || RegisteredTextures.size() >= 4095) return nullptr;
    auto* sampler = (jSamplerStateInfo_Vulkan*)TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();
    const auto set = ImGui_ImplVulkan_AddTexture(sampler->SamplerState,
        static_cast<jTexture_Vulkan*>(texture.get())->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!set) return nullptr;
    RegisteredTextures.emplace_back(set, texture);
    return reinterpret_cast<void*>(set);
}
void jImGUI_Vulkan::Draw(const std::shared_ptr<jRenderFrameContext>& frame)
{
    if (!IsInitialized || !frame || !frame->SceneRenderTargetPtr || !ImGui::GetDrawData()) return;
    auto target = frame->SceneRenderTargetPtr->FinalColorPtr;
    g_rhi->TransitionLayout(frame->GetActiveCommandBuffer(), target->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
    auto* pass = UIRenderPass(target);
    if (pass && pass->BeginRenderPass(frame->GetActiveCommandBuffer()))
    {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)frame->GetActiveCommandBuffer()->GetHandle());
        pass->EndRenderPass();
    }
}
void jImGUI_Vulkan::UnregisterTexture(void* id)
{
    const auto descriptor = reinterpret_cast<VkDescriptorSet>(id);
    const auto found = std::find_if(RegisteredTextures.begin(), RegisteredTextures.end(), [descriptor](const auto& entry) { return entry.first == descriptor; });
    if (found == RegisteredTextures.end()) return;
    ImGui_ImplVulkan_RemoveTexture(descriptor);
    RegisteredTextures.erase(found);
}
void jImGUI_Vulkan::NewFrameInternal()
{
    ImGui_ImplVulkan_NewFrame();
    // GLFW may enumerate no active monitors while macOS displays sleep. Retry
    // its real platform query instead of manufacturing monitor geometry; the
    // official backend normally refreshes only after a monitor event.
    if (ImGui::GetPlatformIO().Monitors.empty()) ImGui_ImplGlfw_MonitorCallback(nullptr, 0);
    ImGui_ImplGlfw_NewFrame();
    if (ImGui::GetPlatformIO().Monitors.empty()) ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    ImGui::NewFrame();
}
