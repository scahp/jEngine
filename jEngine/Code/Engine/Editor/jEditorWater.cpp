#include "pch.h"
#include "jEditorWater.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "imgui.h"

namespace jEditorWater
{
void RenderInspector(jEditor& editor, jEditorAuthoring::EntityId id, const PropertyEdit& apply)
{
    auto& document = editor.GetDocument();
    const auto* original = document.Find(id);
    if (!original || original->Kind != jEditorAuthoring::EntityKind::Water || !apply) return;
    auto candidate = *original;
    const bool disabled = document.IsPlaying() || document.IsEffectivelyLocked(id);
    ImGui::BeginDisabled(disabled);
    std::string validationError;
    const auto finish = [&](bool changed, const char* label)
    {
        // The outer inspector owns transactions. Even an invalid edit reports
        // changed=false so activation/deactivation still reaches that owner.
        if (changed && !Validate(candidate.Water, validationError))
        {
            changed = false;
            if (const auto* current = document.Find(id)) candidate = *current;
        }
        apply(candidate, label, changed && !disabled);
        if (const auto* current = document.Find(id)) candidate = *current;
    };
    ImGui::TextUnformatted("Analytic water surface");
    ImGui::TextWrapped("Distances and speeds use mesh-local units before the entity transform. Play drives wave time; Stop restores the authored surface.");
    ImGui::TextWrapped("Raster water: Forward/Deferred, shadow maps and picking use the animated surface. Ray-tracing acceleration structures and collision remain the base mesh.");
    int resolution = static_cast<int>(candidate.Water.Resolution);
    bool changed = ImGui::SliderInt("Water grid", &resolution, 2, 129);
    if (changed) candidate.Water.Resolution = static_cast<std::uint32_t>(resolution);
    finish(changed, "Water grid");
    finish(ImGui::DragFloat("Water width", &candidate.Water.SizeX, 1.f, .0001f, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp), "Water width");
    finish(ImGui::DragFloat("Water depth", &candidate.Water.SizeZ, 1.f, .0001f, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp), "Water depth");
    finish(ImGui::DragFloat("Water base height", &candidate.Water.Waves.BaseHeight, .25f, -1e8f, 1e8f), "Water base height");
    if (ImGui::Button("Pond wave preset"))
    {
        candidate.Water.Waves.Waves = jEditorAuthoring::WaterProperties{}.Waves.Waves;
        finish(true, "Pond wave preset");
    }
    int count = static_cast<int>(candidate.Water.Waves.Waves.size());
    changed = ImGui::SliderInt("Wave count", &count, 0, static_cast<int>(MaximumGpuWaves));
    if (changed) candidate.Water.Waves.Waves.resize(static_cast<std::size_t>(count));
    finish(changed, "Water wave count");
    for (std::size_t i = 0; i < candidate.Water.Waves.Waves.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode("Wave", "Wave %d", static_cast<int>(i+1)))
        {
            // Refresh candidate after every edit; document mutations invalidate
            // all previously borrowed entity pointers, including references.
            finish(ImGui::DragFloat2("Direction XZ", candidate.Water.Waves.Waves[i].Direction.data(), .01f, -1.f, 1.f), "Water wave direction");
            finish(ImGui::DragFloat("Amplitude", &candidate.Water.Waves.Waves[i].Amplitude, .1f, 0.f, 1e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp), "Water wave amplitude");
            finish(ImGui::DragFloat("Wavelength", &candidate.Water.Waves.Waves[i].Wavelength, 1.f, .0001f, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp), "Water wavelength");
            finish(ImGui::DragFloat("Speed", &candidate.Water.Waves.Waves[i].Speed, .25f, -1e6f, 1e6f), "Water wave speed");
            finish(ImGui::SliderFloat("Steepness Q", &candidate.Water.Waves.Waves[i].Steepness, 0.f, 1.f), "Water wave steepness");
            finish(ImGui::DragFloat("Phase (radians)", &candidate.Water.Waves.Waves[i].PhaseRadians, .01f, -1e8f, 1e8f), "Water wave phase");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (!validationError.empty()) ImGui::TextColored(ImVec4(1.f,.4f,.3f,1.f), "%s", validationError.c_str());
}
}
#endif
