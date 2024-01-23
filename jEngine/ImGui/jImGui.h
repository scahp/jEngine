#pragma once

#include "Math/Vector.h"

class jImGUI
{
public:
    virtual ~jImGUI() {}
    // Initialize styles, keys, etc.
    virtual void Initialize(float width, float height) {}
    virtual void Release() {}

    // Draw current imGui frame into a command buffer
    virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr) {}

    // Starts a new imGui frame and sets up windows and ui elements
    template <typename T>
    void NewFrame(T RenderImGUIfunc) 
    {
        NewFrameInternal();
        RenderImGUIfunc();
        ImGui::Render();
    }

protected:
    virtual void NewFrameInternal() {}
};

extern jImGUI* g_ImGUI;
