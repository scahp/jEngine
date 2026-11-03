#include "pch.h"
#include "Code/Engine/jEditor.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "imgui_internal.h"
#include <fstream>
#include <iomanip>

int RunEditorWorkflowSelfTests()
{
#ifdef ENABLE_EDITOR_FEATURES
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
    };
    const auto oldKeys = g_KeyState;
    auto* previousEditor = g_Editor;
    auto* previousContext = ImGui::GetCurrentContext();
    auto* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(600, 1040);
    io.DeltaTime = 1.0f / 60.0f;
    io.MousePos = ImVec2(-1000, -1000);
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ResetAllKeyState();

    {
        jEditor editor;
        g_Editor = &editor;
        auto& placement = editor.Placement;
        placement.EnablePlacementMode = true;
        jCamera camera;
        jObject first, middle, last, extra;
        auto* mesh = new jRenderObject;
        first.AddRenderObject(mesh);
        jObject::AddObject(&first);
        jObject::AddObject(&middle);
        jObject::AddObject(&last);
        // Only filter metadata is synthetic; selection, transforms and removal
        // below use real registered objects and the production editor paths.
        placement.PlacedObjects[0].ShapeType = EPlacementShapeType::CUBE;
        placement.PlacedObjects[1].ShapeType = EPlacementShapeType::SPHERE;
        placement.PlacedObjects[2].ShapeType = EPlacementShapeType::CUBE;

        const auto process = [&] { placement.ProcessInput(0, &camera, 1); };
        const auto press = [&](EInputKey key) {
            SetKeyDownState(key, false); process();
            SetKeyDownState(key, true); process();
            SetKeyDownState(key, false); process();
        };
        const auto render = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(560, 1000), ImGuiCond_Always);
            ImGui::Begin("Editor workflow test", nullptr, ImGuiWindowFlags_NoSavedSettings);
            placement.RenderUI(nullptr); // UI selection needs no RHI or camera.
            ImGui::End();
            ImGui::Render();
        };

        render(); render();
        ImGuiWindow* list = nullptr;
        for (auto* window : context->Windows)
            if (strstr(window->Name, "PlacementListRegion"))
                list = window;
        if (list)
        {
            // Locate the actual child window and click its second row. This
            // exercises RenderUI/Selectable rather than a copied list renderer.
            io.MousePos = ImVec2(list->DC.CursorStartPos.x + 10,
                list->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFontSize() * 0.5f);
            io.MouseDown[0] = true; render();
            io.MouseDown[0] = false; render();
        }
        expect(list && placement.SelectedPlacedObjectIndex == 1 && placement.PickedObject == &middle,
            "real scene-list click synchronizes inspector and viewport selection");
        io.MousePos = ImVec2(-1000, -1000);
        io.WantCaptureKeyboard = false;
        io.WantTextInput = false;

        placement.SelectedRenderObjectIndices = {0, 1};
        placement.bPickRequested = true;
        placement.SelectPlacedObject(0);
        expect(placement.PickedObject == &first && placement.SelectedRenderObjectIndices.empty()
            && !placement.bPickRequested, "hierarchy selection clears stale mesh selection and pending picks");

        placement.SetSceneFilter("cube");
        expect(placement.GetSceneListIndices() == std::vector<int32>({0, 2}),
            "case-insensitive scene search retains original object indices");
        placement.SelectPlacedObject(-1);
        press(EInputKey::DOWN);
        expect(placement.SelectedPlacedObjectIndex == 0, "Down selects first filtered object");
        press(EInputKey::DOWN);
        expect(placement.SelectedPlacedObjectIndex == 2 && placement.PickedObject == &last,
            "Down skips objects hidden by the search");
        press(EInputKey::UP);
        press(EInputKey::UP);
        expect(placement.SelectedPlacedObjectIndex == 0, "Up remains within filtered scene bounds");
        placement.SetSceneFilter("cube,-[0]");
        expect(placement.GetSceneListIndices() == std::vector<int32>({2}),
            "scene search uses the bundled ImGui exclusion syntax");
        placement.SetSceneFilter("no matching objects");
        press(EInputKey::DOWN);
        expect(placement.GetSceneListIndices().empty() && placement.SelectedPlacedObjectIndex == 0,
            "empty search results preserve the existing inspector selection");

        placement.SetSceneFilter("");
        io.WantTextInput = true;
        SetKeyDownState(EInputKey::DELETE_KEY, true); process();
        io.WantTextInput = false; process();
        expect(placement.PlacedObjects.size() == 3,
            "Delete held while typing is consumed before text focus is released");
        SetKeyDownState(EInputKey::DELETE_KEY, false); process();
        placement.EnablePlacementMode = false;
        SetKeyDownState(EInputKey::DELETE_KEY, true); process();
        placement.EnablePlacementMode = true; process();
        expect(placement.PlacedObjects.size() == 3,
            "inactive placement panel cannot delete and consumes held key edges");
        SetKeyDownState(EInputKey::DELETE_KEY, false); process();

        const auto transformApplied = placement.ApplySelectedTransform(Vector(750, -900, 1250),
            Vector(0, 0, 90), Vector(2, 3, 4));
        mesh->UpdateWorldMatrix();
        const auto transformed = mesh->World.TransformPoint(Vector(1, 0, 0));
        expect(transformApplied && std::abs(mesh->GetRot().z - PI * 0.5f) < 0.0001f
            && std::abs(transformed.x - 750) < 0.001f && std::abs(transformed.y + 898) < 0.001f
            && std::abs(transformed.z - 1250) < 0.001f,
            "inspector degrees become runtime radians and produce the expected world transform beyond old sliders");
        const auto savedPosition = mesh->GetPos();
        const auto savedRotation = mesh->GetRot();
        const auto savedScale = mesh->GetScale();
        expect(!placement.ApplySelectedTransform(Vector(std::numeric_limits<float>::quiet_NaN(), 0, 0), Vector::ZeroVector, Vector::OneVector)
            && !placement.ApplySelectedTransform(Vector::ZeroVector, Vector(std::numeric_limits<float>::infinity(), 0, 0), Vector::OneVector)
            && !placement.ApplySelectedTransform(Vector::ZeroVector, Vector::ZeroVector, Vector(1, 0, 1))
            && mesh->GetPos().IsNearlyEqual(savedPosition) && mesh->GetRot().IsNearlyEqual(savedRotation)
            && mesh->GetScale().IsNearlyEqual(savedScale),
            "invalid numeric transforms leave all live render-object components unchanged");
        expect(placement.ApplySelectedTransform(savedPosition, Vector(0, 0, 90), Vector(0.001f)),
            "the documented minimum positive scale is accepted consistently");

        float translation[] = {10, 20, 30}, rotation[] = {0, 90, 0}, scale[] = {1, 1, 1};
        float matrix[16], decomposedPosition[3], decomposedRotation[3], decomposedScale[3];
        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, matrix);
        ImGuizmo::DecomposeMatrixToComponents(matrix, decomposedPosition, decomposedRotation, decomposedScale);
        expect(placement.ApplySelectedTransform(Vector(decomposedPosition[0], decomposedPosition[1], decomposedPosition[2]),
            Vector(decomposedRotation[0], decomposedRotation[1], decomposedRotation[2]),
            Vector(decomposedScale[0], decomposedScale[1], decomposedScale[2]))
            && std::abs(mesh->GetRot().y - PI * 0.5f) < 0.0001f,
            "actual ImGuizmo degree decomposition uses the same validated runtime transform path");

        const bool oldSnap = placement.UseSnap;
        SetKeyDownState(EInputKey::CTRL, true); process();
        expect(placement.UseSnap == oldSnap, "modifier-only Ctrl does not silently toggle snapping");
        SetKeyDownState(EInputKey::G, true); process(); process();
        expect(placement.UseSnap != oldSnap, "Ctrl+G toggles snapping once while held");
        SetKeyDownState(EInputKey::G, false);
        SetKeyDownState(EInputKey::CTRL, false); process();

        placement.SetSceneFilter("Scene Object");
        jObject::AddObject(&extra);
        expect(placement.GetSceneListIndices() == std::vector<int32>({3}),
            "scene registration invalidates an already populated search cache");
        jObject::RemoveObject(&extra);
        expect(placement.GetSceneListIndices().empty(), "scene removal invalidates filtered row indices");
        placement.SetSceneFilter("sphere");
        placement.SelectPlacedObject(1);
        press(EInputKey::DELETE_KEY);
        expect(placement.PlacedObjects.size() == 2 && placement.GetSceneListIndices().empty()
            && std::find(jObject::GetStaticObject().begin(), jObject::GetStaticObject().end(), &middle)
                == jObject::GetStaticObject().end(),
            "Delete removes the actual filtered scene object and refreshes the search cache");
        press(EInputKey::ESCAPE);
        expect(placement.SelectedPlacedObjectIndex == -1 && !placement.PickedObject,
            "Escape clears both hierarchy and viewport selection");
        placement.SelectPlacedObject(100);
        expect(placement.SelectedPlacedObjectIndex == -1, "stale out-of-range row selection is rejected");
        jObject::RemoveObject(&first);
        jObject::RemoveObject(&last);
        placement.Clear();
        expect(placement.GetSceneListIndices().empty(), "clearing the scene removes all cached rows");
        g_Editor = previousEditor;
    }
    g_KeyState = oldKeys;
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
    return failures;
#else
    return 0;
#endif
}

// This benchmark deliberately calls the same public UI used by the renderer.
// It needs neither a window backend nor an RHI, and never creates scene/GPU data.
int RunEditorWorkflowBenchmarks(const char* outputPath)
{
#ifdef ENABLE_EDITOR_FEATURES
    if (!outputPath || !*outputPath)
        return EXIT_FAILURE;

    std::ofstream output(outputPath);
    if (!output)
        return EXIT_FAILURE;

    auto* previousContext = ImGui::GetCurrentContext();
    auto* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(600, 1040);
    io.DeltaTime = 1.0f / 60.0f;
    io.MousePos = ImVec2(-1000, -1000);
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0, fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

    constexpr int warmupFrames = 30;
    constexpr int measuredFrames = 120;
    output << std::fixed << std::setprecision(3)
        << "{\n  \"benchmark\": \"public-placement-render-ui\",\n"
        << "  \"imgui_version\": \"" << ImGui::GetVersion() << "\",\n"
#ifdef NDEBUG
        << "  \"build\": \"Release\",\n"
#else
        << "  \"build\": \"Debug\",\n"
#endif
        << "  \"display\": [600, 1040],\n  \"window\": [560, 1000],\n"
        << "  \"warmup_frames\": " << warmupFrames << ",\n"
        << "  \"measured_frames\": " << measuredFrames << ",\n"
        << "  \"conditions\": \"Fresh ImGui context; default font/style; no backend/GPU; "
           "null object payloads; no selection, search, scrolling or input; frame CPU includes "
           "NewFrame/Begin/RenderUI/End/Render. Layout changes can alter visible rows and vertices.\",\n"
        << "  \"scenes\": [\n";

    bool firstScene = true;
    for (const int rowCount : {100, 1000, 10000})
    {
        jPlacementTool placement;
        placement.EnablePlacementMode = true;
        placement.PlacedObjects.resize(rowCount);
        for (auto& info : placement.PlacedObjects)
            info.Type = EPlacedObjectType::SHAPE;
        jCamera camera;
        std::vector<double> frameMicroseconds;
        frameMicroseconds.reserve(measuredFrames);
        int vertices = 0, indices = 0;

        for (int frame = 0; frame < warmupFrames + measuredFrames; ++frame)
        {
            const auto start = std::chrono::steady_clock::now();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(560, 1000), ImGuiCond_Always);
            ImGui::Begin("Placement UI benchmark", nullptr,
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            placement.RenderUI(&camera);
            ImGui::End();
            ImGui::Render();
            const auto end = std::chrono::steady_clock::now();
            if (frame >= warmupFrames)
                frameMicroseconds.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            vertices = ImGui::GetDrawData()->TotalVtxCount;
            indices = ImGui::GetDrawData()->TotalIdxCount;
        }

        std::sort(frameMicroseconds.begin(), frameMicroseconds.end());
        const double median = (frameMicroseconds[measuredFrames / 2 - 1]
            + frameMicroseconds[measuredFrames / 2]) * 0.5;
        const double p95 = frameMicroseconds[static_cast<int>(std::ceil(measuredFrames * 0.95)) - 1];
        if (!firstScene)
            output << ",\n";
        firstScene = false;
        output << "    {\"rows\": " << rowCount << ", \"median_us\": " << median
            << ", \"p95_us\": " << p95 << ", \"draw_vertices\": " << vertices
            << ", \"draw_indices\": " << indices << "}";
        std::cout << "Placement UI " << rowCount << " rows: median " << median
            << " us, p95 " << p95 << " us, vertices " << vertices << std::endl;
    }
    output << "\n  ]\n}\n";
    output.flush();
    const bool written = output.good();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
    return written ? EXIT_SUCCESS : EXIT_FAILURE;
#else
    (void)outputPath;
    return EXIT_FAILURE;
#endif
}
