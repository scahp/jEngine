# Dear ImGui upstream

- Project: https://github.com/ocornut/imgui
- Version: v1.89.9-docking
- Commit: b4c31b3a904127518ea41f7f42e0087b731040b9
- License: MIT, preserved in LICENSE.txt and source headers.
- Source archive: https://codeload.github.com/ocornut/imgui/tar.gz/b4c31b3a904127518ea41f7f42e0087b731040b9
- Archive SHA-256: ba3857f972fc5459d46716bf0e1359174e5dc2960c882be5e03a4406a7c07439
- Core and selected official backends are flattened into this directory to preserve existing include/build paths.
- Local patch: DX12 optional shared platform command queue, documented in imgui_impl_dx12.h/.cpp; it keeps detached viewport reads ordered after the engine scene-preview copies on the same graphics queue.
- Local patch: Vulkan helper windows create their own pipeline against their actual render pass after renderer initialization, including resize/out-of-date recreation. The engine main UI pass has a different dependency topology from the official secondary helper pass (two dependencies versus one); reusing the main pipeline caused `VUID-vkCmdDrawIndexed-renderPass-02684`. Existing `wd->Pipeline` draw/destruction ownership is retained; no dependency or validation check is removed. Old helper pipeline handles are nulled after destruction. Dynamic rendering and helper use before renderer initialization keep the upstream behavior.
- Local configuration: imconfig.h enables IMGUI_DEFINE_MATH_OPERATORS before the shared PCH includes imgui.h, as required by the bundled ImGuizmo.
- Legacy ui.vert.spv/ui.frag.spv remain historical custom-backend assets; the official Vulkan backend embeds its own shaders.

Docking and platform windows are provided by the pinned docking branch. Platform windows remain an explicit editor preference; renderer support is reported only after the platform and renderer callbacks are installed.
