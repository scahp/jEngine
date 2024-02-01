#pragma once

#include "Math/Vector.h"
#include "Profiler/jPerformanceProfile.h"

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

    virtual float GetCurrentMonitorDPIScale() const { return 1.0f; }

    // UI Generator Utility
    static void CreateTreeForProfiling(const char* InTreeTitle, const jPerformanceProfile::AvgProfileMapType& InProfileData, double InTotalProfileMS, float InTabSpacing = 10.0f);

protected:
    virtual void NewFrameInternal() {}
};

extern jImGUI* g_ImGUI;
