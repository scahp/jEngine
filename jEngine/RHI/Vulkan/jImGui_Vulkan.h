#pragma once
#include "Math/Vector.h"
#include "ImGui/jImGui.h"

// Official Dear ImGui Vulkan renderer owns font, pipeline and detached-window
// resources. Engine-owned texture leases keep ImTextureID descriptors alive.
class jImGUI_Vulkan : public jImGUI
{
public:
    jImGUI_Vulkan();
    ~jImGUI_Vulkan() override;
    void Initialize(float width, float height) override;
    void Release() override;
    void Draw(const std::shared_ptr<jRenderFrameContext>& frame) override;
    float GetCurrentMonitorDPIScale() const override { return MonitorDPIScale; }
    void* RegisterTexture(const std::shared_ptr<jTexture>& texture);
    void UnregisterTexture(void* id); // Caller has drained all UI users.
    robin_hood::unordered_map<jName, uint64, jNameHashFunc> CounterMap;
private:
    void NewFrameInternal() override;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::vector<std::pair<VkDescriptorSet, std::shared_ptr<jTexture>>> RegisteredTextures;
    float MonitorDPIScale = 1.0f;
    bool IsInitialized = false;
    bool PlatformInitialized = false;
};
