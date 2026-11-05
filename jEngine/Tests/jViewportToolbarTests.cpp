#include "pch.h"
#include "jViewportToolbarTests.h"

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "Scene/jCamera.h"
#include "jEngine.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <cmath>
#include <fstream>

namespace
{
using Json = nlohmann::json;
struct ViewRect
{
    float X = 0, Y = 0, Width = 0, Height = 0;
    bool Contains(const ImVec2& point) const
    {
        return point.x > X + 2 && point.y > Y + 2
            && point.x < X + Width - 2 && point.y < Y + Height - 2;
    }
};
bool Project(const Matrix& matrix, const Vector& position, const ViewRect& view, ImVec2& screen)
{
    const auto clip = matrix.Transform(Vector4(position, 1));
    if (!std::isfinite(clip.w) || clip.w <= 0.00001f) return false;
    screen = ImVec2(view.X + (clip.x / clip.w * .5f + .5f) * view.Width,
        view.Y + (.5f - clip.y / clip.w * .5f) * view.Height);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}
void MoveMouse(const ImVec2& point)
{
    if (IsUseVulkan())
    {
        auto* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
        int x = 0, y = 0;
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) glfwGetWindowPos(window, &x, &y);
        glfwSetCursorPos(window, point.x - x, point.y - y);
    }
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
}
}
#endif

int RunViewportToolbarGizmoGpuTests(const std::filesystem::path& output,
    const std::function<void()>& frame)
{
#ifdef ENABLE_EDITOR_FEATURES
    Json report{{"status", "failed"}, {"checks", Json::array()}, {"attempts", Json::array()}};
    int failures = 0;
    unsigned frames = 0;
    bool applicable = true, touchedInput = false;
    jEditor* editor = nullptr;
    std::vector<jEditor::EntityId> originalSelection;
    ImGuizmo::OPERATION oldOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE oldMode = ImGuizmo::WORLD;
    bool oldSnap = false, oldWorkspace = false, haveState = false;
    const auto expect = [&](bool pass, const char* name)
    {
        report["checks"].push_back({{"name", name}, {"pass", pass}, {"frame", frames}});
        std::cout << (pass ? "PASS " : "FAIL ") << "viewport toolbar gizmo: " << name << std::endl;
        failures += !pass;
    };
    const auto tick = [&]
    {
        if (!frame || frames >= 70) throw std::runtime_error("Viewport toolbar helper exceeded its 70-frame bound");
        ++frames;
        frame();
    };
    const auto button = [&](bool down)
    {
        touchedInput = true;
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        tick();
    };
    const auto closePopup = [&]
    {
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
        {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true); tick();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); tick();
        }
    };
    try
    {
        if (output.empty() || !frame || !g_Engine || !g_Editor || !g_rhi || !ImGui::GetCurrentContext())
            throw std::runtime_error("Viewport toolbar test requires initialized engine/editor/UI and an owned output directory");
        editor = g_Editor;
        auto& document = editor->GetDocument();
        const auto selected = editor->GetSelectedEntityId();
        if (!selected || document.Entities().empty())
        {
            applicable = false;
            report["reason"] = !selected ? "No selected authoring entity" : "Legacy scene has no authoring entities";
            std::cout << "NOT_APPLICABLE viewport toolbar gizmo: " << report["reason"].get<std::string>() << std::endl;
        }
        else
        {
            auto* camera = g_Engine->Game.MainCamera;
            if (!camera || !document.Find(selected) || document.IsPlaying() || document.HasTransaction()
                || document.IsEffectivelyLocked(selected) || !document.IsEffectivelyVisible(selected))
                throw std::runtime_error("Selected gizmo fixture must be visible/editable with an idle document and camera");
            originalSelection = editor->GetSelectedEntities();
            oldOperation = editor->Placement.GizmoOperation; oldMode = editor->Placement.GizmoMode;
            oldSnap = editor->Placement.UseSnap; oldWorkspace = IsAuthoringWorkspaceVisible(); haveState = true;
            SetAuthoringWorkspaceVisible(true);
            closePopup();
            editor->SetSelectedEntities({selected});
            editor->Placement.GizmoOperation = ImGuizmo::TRANSLATE;
            editor->Placement.UseSnap = false;
            tick();
            ViewRect view;
            if (!GetAuthoringViewportRect(view.X, view.Y, view.Width, view.Height))
                throw std::runtime_error("Scene/Game viewport image is unavailable");
            Matrix world;
            if (!document.GetWorldMatrix(selected, world.mm))
                throw std::runtime_error("Cannot resolve selected entity world matrix");
            ImVec2 origin;
            if (!Project(camera->ViewProjection * world, Vector::ZeroVector, view, origin) || !view.Contains(origin))
                throw std::runtime_error("Selected gizmo origin is outside the viewport image");
            report["selectedEntity"] = selected;
            report["origin"] = {origin.x, origin.y};
            const auto baseline = document.ToJson();
            const auto cameraPosition = camera->Pos, cameraTarget = camera->Target, cameraUp = camera->Up;
            const auto cameraUnchanged = [&]
            {
                return camera->Pos.IsNearlyEqual(cameraPosition) && camera->Target.IsNearlyEqual(cameraTarget)
                    && camera->Up.IsNearlyEqual(cameraUp);
            };
            const Vector center(world.m30, world.m31, world.m32);
            const Vector axes[] = {Vector(1, 0, 0), Vector(0, 1, 0), Vector(0, 0, 1)};
            const float probeLength = std::max(1.f, (center - camera->Pos).Length() * .01f);
            bool dragging = false;
            // At most twelve candidates: three projected axes, 20/30 px, and
            // WORLD/LOCAL modes. ImGuizmo flips an axis toward the longer visible
            // projected segment; reproduce that choice without private state.
            for (const auto mode : {ImGuizmo::WORLD, ImGuizmo::LOCAL})
            {
                editor->Placement.GizmoMode = mode;
                for (int axis = 0; axis < 3 && !dragging; ++axis)
                {
                    Vector direction = mode == ImGuizmo::LOCAL ? world.TransformDirection(axes[axis]) : axes[axis];
                    if (direction.LengthSQ() < .00001f) continue;
                    direction = direction.GetNormalize() * probeLength;
                    ImVec2 positive, negative;
                    if (!Project(camera->ViewProjection, center + direction, view, positive)
                        || !Project(camera->ViewProjection, center - direction, view, negative)) continue;
                    const float positiveLength = std::hypot(positive.x - origin.x, positive.y - origin.y);
                    const float negativeLength = std::hypot(negative.x - origin.x, negative.y - origin.y);
                    const auto endpoint = negativeLength > positiveLength ? negative : positive;
                    const float length = std::max(positiveLength, negativeLength);
                    if (length < .01f) continue;
                    const ImVec2 screenDirection((endpoint.x - origin.x) / length, (endpoint.y - origin.y) / length);
                    for (const float distance : {20.f, 30.f})
                    {
                        const ImVec2 point(origin.x + screenDirection.x * distance, origin.y + screenDirection.y * distance);
                        const ImVec2 moved(point.x + screenDirection.x * 8, point.y + screenDirection.y * 8);
                        if (!view.Contains(point) || !view.Contains(moved)) continue;
                        editor->SetSelectedEntities({selected});
                        MoveMouse(point); tick();
                        button(true);
                        const bool acquired = ImGuizmo::IsUsing();
                        if (acquired) { MoveMouse(moved); tick(); }
                        dragging = acquired && ImGuizmo::IsUsing() && document.HasTransaction() && document.ToJson() != baseline;
                        report["attempts"].push_back({{"mode", mode == ImGuizmo::WORLD ? "world" : "local"},
                            {"axis", axis}, {"distancePixels", distance}, {"point", {point.x, point.y}},
                            {"acquired", acquired}, {"transaction", document.HasTransaction()}, {"dragging", dragging}, {"frame", frames}});
                        if (dragging) break;
                        button(false);
                        if (document.HasTransaction()) throw std::runtime_error("Failed handle attempt left an edit transaction active");
                        editor->SetSelectedEntities({selected});
                        if (document.ToJson() != baseline && (!document.Undo() || document.ToJson() != baseline))
                            throw std::runtime_error("Failed handle attempt could not restore baseline through Undo");
                    }
                }
                if (dragging) break;
            }
            if (!dragging) throw std::runtime_error("No projected 20/30-pixel gizmo handle started a real drag");
            expect(dragging, "actual mouse drag acquires ImGuizmo and creates a changed document transaction");
            ViewRect control;
            if (!GetAuthoringRendererControlRect(control.X, control.Y, control.Width, control.Height))
                throw std::runtime_error("Viewport renderer combo is unavailable");
            const ImVec2 toolbar(control.X + control.Width * .5f, control.Y + control.Height * .5f);
            MoveMouse(toolbar); tick(); // Deliberately retain left-down across the image/toolbar boundary.
            expect(ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGuizmo::IsUsing() && document.HasTransaction(),
                "an existing drag stays active while held over the renderer toolbar");
            button(false);
            expect(!ImGuizmo::IsUsing() && !document.HasTransaction(),
                "release over the toolbar ends ImGuizmo and commits the original drag transaction");
            const auto released = document.ToJson();
            MoveMouse(toolbar); tick(); button(true); button(false); tick();
            expect(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)
                && !ImGuizmo::IsUsing() && !document.HasTransaction()
                && !IsAuthoringViewportHovered() && !IsAuthoringViewportFocused()
                && editor->GetSelectedEntityId() == selected && document.ToJson() == released && cameraUnchanged(),
                "actual renderer combo click opens its popup without scene input or another gizmo edit");
            closePopup();
            expect(document.CanUndo() && document.Undo() && document.ToJson() == baseline,
                "completed drag can be undone to the exact authored baseline");
            std::filesystem::create_directories(output);
            const auto scene = output / "viewport-toolbar-undo.jscene";
            const bool saved = document.Save(scene, false);
            jEditorAuthoring::Document savedDocument;
            expect(saved && savedDocument.Load(scene) && savedDocument.ToJson() == baseline,
                "scene Save writes the restored baseline after the toolbar interaction");
            const bool playing = editor->BeginPlay();
            if (playing) tick();
            const bool stopped = playing && editor->StopPlay();
            if (stopped) tick();
            expect(playing && stopped && !document.IsPlaying() && !document.HasTransaction() && document.ToJson() == baseline,
                "Play and Stop remain available after release and Undo");
        }
    }
    catch (const std::exception& error)
    {
        ++failures; report["exception"] = error.what();
        std::cerr << "FAIL viewport toolbar gizmo: " << error.what() << std::endl;
    }
    if (haveState && editor && ImGui::GetCurrentContext())
    {
        // Cleanup occurs only after all assertions. Never repair a stuck release
        // before checking Undo/Save/Play, since that would hide the regression.
        if (touchedInput) ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        if (editor->GetDocument().IsPlaying()) editor->StopPlay();
        if (failures)
        {
            ImGuizmo::Enable(false);
            if (editor->GetDocument().HasTransaction()) editor->GetDocument().CancelTransaction();
            ImGuizmo::Enable(true);
        }
        editor->Placement.GizmoOperation = oldOperation; editor->Placement.GizmoMode = oldMode;
        editor->Placement.UseSnap = oldSnap; editor->SetSelectedEntities(originalSelection);
        SetAuthoringWorkspaceVisible(oldWorkspace);
    }
    report["renderedFrames"] = frames; report["frameBudget"] = 70;
    report["failureCount"] = failures;
    report["status"] = failures ? "failed" : applicable ? "passed" : "not-applicable";
    try
    {
        if (output.empty()) throw std::runtime_error("Report output directory is empty");
        std::filesystem::create_directories(output);
        std::ofstream file(output / "viewport-toolbar-gizmo-report.json", std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << report.dump(2) << '\n';
    }
    catch (const std::exception& error)
    {
        ++failures;
        report["status"] = "failed"; report["failureCount"] = failures;
        std::cerr << "FAIL viewport toolbar report: " << error.what() << std::endl;
    }
    std::cout << "Viewport toolbar gizmo workflow: " << report["status"].get<std::string>()
        << ", " << frames << " frames, " << failures << " failures" << std::endl;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
#else
    (void)output; (void)frame;
    std::cerr << "FAIL viewport toolbar gizmo: editor features are disabled" << std::endl;
    return EXIT_FAILURE;
#endif
}
