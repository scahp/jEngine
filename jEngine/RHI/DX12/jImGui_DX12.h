#pragma once

#include "Math/Vector.h"
#include "RHI/DX12/jBuffer_DX12.h"
#include "ImGui/jImGui.h"

class jImGUI_DX12 : public jImGUI
{
public:
    jImGUI_DX12();
    virtual ~jImGUI_DX12();

    // Initialize styles, keys, etc.
    virtual void Initialize(float width, float height) override;
    virtual void Release() override;

    // Draw current imGui frame into a command buffer
    virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr);

    // Starts a new imGui frame and sets up windows and ui elements
    virtual void NewFrameInternal() override;

    bool IsInitialized = false;
    void* RegisterTexture(const std::shared_ptr<jTexture>& texture);
    void UnregisterTexture(void* id); // Caller has drained all UI users.

private:
    ComPtr<ID3D12DescriptorHeap> m_imgui_SrvDescHeap;
    uint32 NextTextureDescriptor = 1; // Zero belongs to the font atlas.
    std::vector<uint32> FreeTextureDescriptors;
    std::vector<std::shared_ptr<jTexture>> RegisteredTextures;
};
