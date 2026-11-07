#include "Profiler/jRenderTrace.h"
#include <iostream>

int main()
{
    using namespace jRenderTrace;
    int failures = 0;
    const auto expect = [&](bool value, const char* label) {
        if (!value) { std::cerr << "FAIL " << label << '\n'; ++failures; }
    };
    Reset(); BeginFrame(1, "Deferred", "Vulkan", 960, 540);
    expect(!BeginPass("Disabled", 0).Frame, "disabled recording creates no pass");
    EndFrame(); SetEnabled(true); BeginFrame(10, "Deferred", "Vulkan", 960, 540);
    const auto parent = BeginPass("BasePass", 0);
    const auto child = BeginPass("Lighting", 0); RecordCommand(false); RecordCommand(true);
    EndPass(child); EndPass(parent);
    Resource resource; resource.Key = "Color"; resource.Producer = "BasePass";
    RecordResource(resource); resource.Available = true; RecordResource(resource); EndFrame();
    const auto pendingCopy = CopyFrame(10);
    expect(pendingCopy && pendingCopy->Passes[1].Parent == parent.Pass, "nested scopes retain parents");
    expect(pendingCopy->Passes[1].DrawCalls == 1 && pendingCopy->Passes[1].ComputeCalls == 1, "self command counts");
    expect(pendingCopy->Resources.size() == 1 && pendingCopy->Resources[0].Available &&
        pendingCopy->Resources[0].ProducerId == parent.Pass, "placeholder replaced by actual output");
    expect(!LatestCompletedFrame(), "incomplete GPU data not advertised as a completed frame");
    BeginFrame(11, "Forward", "DX12", 960, 540); auto later = BeginPass("BasePass", 1); EndPass(later); EndFrame();
    CompleteGpu(child, 250000); CompleteGpu(parent, 750000); CompleteGpu(parent, 999999);
    auto complete = LatestCompletedFrame();
    expect(complete && complete->Id == 10 && complete->Passes[0].GpuMilliseconds == .75, "late GPU result attaches to its original frame once");
    expect(pendingCopy->PendingGpu == 2 && !pendingCopy->Passes[0].GpuReady, "UI snapshot never mutated by delayed GPU completion");
    CompleteGpu(later, 1000000); complete = LatestCompletedFrame();
    expect(complete && complete->Id == 11 && complete->Passes[0].Key == pendingCopy->Passes[0].Key, "pass identity stable across frame and API");
    BeginFrame(12, "Deferred", "Vulkan", 960, 540);
    auto first = BeginPass("Repeated", 0); EndPass(first); auto second = BeginPass("Repeated", 0); EndPass(second); EndFrame();
    expect(CopyFrame(12)->Passes[0].Key != CopyFrame(12)->Passes[1].Key, "repeated names retain distinct identities");
    for (uint64_t frame = 13; frame < 50; ++frame) { BeginFrame(frame, "Deferred", "Vulkan", 960, 540); EndFrame(); }
    CompleteGpu(child, 5000000);
    expect(!CopyFrame(10) && LatestCompletedFrame()->Id == 49, "evicted query result cannot corrupt a later frame");
    BeginFrame(49, "Deferred", "Vulkan", 960, 540);
    expect(!BeginPass("Reused frame", 0).Frame, "frame IDs cannot be reused"); EndFrame();
    Reset(); expect(!IsEnabled() && !LatestCompletedFrame(), "shutdown clears retained traces");
    const auto lease = BeginCapture(); SetEnabled(false);
    expect(IsEnabled(), "external capture does not depend on inspector visibility");
    const auto secondLease = BeginCapture(); EndCapture(lease); EndCapture(lease);
    expect(IsEnabled(), "ending one lease cannot disable another client");
    Reset(); const auto newLease = BeginCapture(); EndCapture(secondLease);
    expect(IsEnabled(), "stale lease cannot end a post-reset capture");
    EndCapture(newLease); expect(!IsEnabled(), "last capture lease releases telemetry");
    std::cout << "Render trace lifecycle: " << failures << " failures\n";
    return failures ? 1 : 0;
}
