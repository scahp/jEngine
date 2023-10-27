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

private:
    ComPtr<ID3D12DescriptorHeap> m_imgui_SrvDescHeap;
};