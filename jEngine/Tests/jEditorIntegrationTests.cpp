#include "pch.h"
#include "jEditorIntegrationTests.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Code/Engine/Editor/jEditorUiRenderer.h"
#include "FileLoader/jEditorAssets.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"
#include "Material/jMaterial.h"
#include "RHI/Vulkan/jBufferUtil_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jBufferUtil_DX12.h"
#endif
#include "jEngine.h"
#include "jOptions.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace jEditorAuthoring;
struct ProbeState
{
    bool Active = false;
    uint32_t Frames = 0, MaximumViewports = 0, SecondaryRenderedFrames = 0;
    ImGuiID DetachedId = 0;
};
ProbeState& Probe() { static ProbeState probe; return probe; }
bool MatrixNear(const Matrix& a, const Matrix& b)
{
    for (int i = 0; i < 16; ++i)
        if (std::abs(a.mm[i] - b.mm[i]) > .002f * std::max(1.0f, std::abs(a.mm[i]))) return false;
    return true;
}
void Write(const fs::path& file, const std::string& text)
{
    std::ofstream output(file, std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit); output << text;
}
void WriteModelFixture(const fs::path& assets)
{
    fs::create_directories(assets);
    Write(assets / "triangle.obj", "mtllib triangle.mtl\nusemtl fixture\n"
        "v -15 0 0\nv 15 0 0\nv 0 30 0\nvt 0 0\nvt 1 0\nvt 0.5 1\nvn 0 0 -1\nf 1/1/1 3/3/1 2/2/1\n");
    Write(assets / "triangle.mtl", "newmtl fixture\nKd 1 1 1\nmap_Kd color.png\n");
    const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,13,73,68,65,84,120,156,99,248,207,192,240,31,0,5,0,1,255,137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130};
    std::ofstream output(assets / "color.png", std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.write(reinterpret_cast<const char*>(png), sizeof(png));
}
jRenderObject* FirstMesh(jEditor& editor, EntityId id)
{
    auto* object = editor.ObjectForEntity(id);
    return object && !object->RenderObjects.empty() ? object->RenderObjects.front() : nullptr;
}
bool BufferMatches(jBuffer* source, const IStreamParam& expected)
{
    const uint64 bytes = expected.GetBufferSize();
    if (!source || !bytes || source->GetBufferSize() < bytes) return false;
    g_rhi->Flush();
    const auto previous = source->GetLayout();
    auto readback = g_rhi->CreateRawBuffer(bytes, 0, EBufferCreateFlag::Readback,
        IsUseVulkan() ? EResourceLayout::UNDEFINED : EResourceLayout::TRANSFER_DST);
    if (!readback || !readback->GetHandle()) return false;
    auto* command = g_rhi->BeginSingleTimeCommands();
    if (!command) return false;
    g_rhi->TransitionLayout(command, source, EResourceLayout::TRANSFER_SRC);
    g_rhi->TransitionLayout(command, readback.get(), EResourceLayout::TRANSFER_DST);
    command->FlushBarrierBatch();
    if (IsUseVulkan())
    {
        auto* sourceVk = static_cast<jBuffer_Vulkan*>(source);
        auto* targetVk = static_cast<jBuffer_Vulkan*>(readback.get());
        VkBufferCopy copy{source->GetOffset(), readback->GetOffset(), bytes};
        const auto native = static_cast<jCommandBuffer_Vulkan*>(command)->GetRef();
        vkCmdCopyBuffer(native, sourceVk->Buffer, targetVk->Buffer, 1, &copy);
        VkBufferMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = targetVk->Buffer; barrier.offset = readback->GetOffset(); barrier.size = bytes;
        vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
#ifdef _WIN32
    else
        static_cast<jCommandBuffer_DX12*>(command)->Get()->CopyBufferRegion(
            static_cast<ID3D12Resource*>(readback->GetHandle()), readback->GetOffset(),
            static_cast<ID3D12Resource*>(source->GetHandle()), source->GetOffset(), bytes);
#endif
    g_rhi->TransitionLayout(command, source, previous == EResourceLayout::UNDEFINED ? EResourceLayout::GENERAL : previous);
    g_rhi->EndSingleTimeCommands(command);
    const void* mapped = readback->Map();
    if (!mapped) return false;
    const bool matches = std::memcmp(mapped, expected.GetBufferData(), size_t(bytes)) == 0;
    readback->Unmap(); return matches;
}
} // namespace
#endif

void RenderEditorIntegrationProbe()
{
#ifdef ENABLE_EDITOR_FEATURES
    auto& probe = Probe();
    if (!probe.Active || !ImGui::GetCurrentContext()) return;
    auto* main = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(main->Pos.x + main->Size.x + 40, main->Pos.y + 70), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 220), ImGuiCond_Always);
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    if (ImGui::Begin("Detached editor GPU integration probe", nullptr,
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextUnformatted("Real platform window / real renderer");
        ImGui::Text("Owner-thread frame %u", probe.Frames);
        ImGui::Separator();
        ImGui::TextWrapped("The integration test verifies an OS window, renderer-owned viewport data and submitted frames.");
        ImGui::ProgressBar(float(probe.Frames % 120) / 120, ImVec2(-1, 0));
        auto* viewport = ImGui::GetWindowViewport();
        if (viewport && viewport != main) probe.DetachedId = viewport->ID;
    }
    ImGui::End();
#endif
}

int RunEditorAuthoringGpuTests(const char* outputDirectory)
{
#ifdef ENABLE_EDITOR_FEATURES
    Json report{{"schemaVersion", 1}, {"test", "editor-authoring-gpu"}, {"checks", Json::array()},
        {"scope", "Actual initialized engine, GPU resources, owner-thread frame lifecycle and platform windows"},
        {"frameCpuMilliseconds", Json::array()}};
    int failures = 0;
    uint32_t rendered = 0;
    fs::path output;
    auto expect = [&](bool pass, const char* label, const std::string& detail = {}) {
        report["checks"].push_back({{"pass", pass}, {"name", label}, {"detail", detail}, {"renderedFrames", rendered}});
        std::cout << (pass ? "PASS " : "FAIL ") << "editor GPU flow: " << label;
        if (!detail.empty()) std::cout << " (" << detail << ')';
        std::cout << std::endl;
        failures += !pass;
        // Publish progress too, so a driver/validation crash leaves useful evidence.
        if (!output.empty())
        {
            auto progress = report; progress["status"] = "running";
            Write(output / "editor-authoring-gpu-progress.json", progress.dump(2) + "\n");
        }
    };
    const auto oldKeys = g_KeyState;
    const auto oldMouse = g_MouseState;
    const bool oldWorkspace = IsAuthoringWorkspaceVisible();
    Probe() = {};
    const auto frame = [&] {
        if (rendered >= 240) throw std::runtime_error("Bounded editor GPU test exceeded 240 frames");
        if (!g_rhi || !g_Engine) throw std::runtime_error("Engine/RHI lifetime ended during test");
        const auto frameStart = std::chrono::steady_clock::now();
        ResetAllKeyState(); ResetMouseAllState();
        g_timeDeltaSecond = 1.0f / 60;
        g_Engine->ProcessInput(g_timeDeltaSecond);
        g_rhi->IncrementFrameNumber();
        g_Engine->Update(g_timeDeltaSecond);
        g_Engine->Draw();
        g_Engine->EndOfFrame();
        ++rendered; Probe().Frames = rendered;
        if (ImGui::GetCurrentContext())
        {
            const auto& viewports = ImGui::GetPlatformIO().Viewports;
            Probe().MaximumViewports = std::max(Probe().MaximumViewports, uint32_t(viewports.Size));
            for (int i = 1; i < viewports.Size; ++i)
                if (viewports[i]->ID == Probe().DetachedId && viewports[i]->PlatformHandle && viewports[i]->RendererUserData)
                { ++Probe().SecondaryRenderedFrames; break; }
        }
#ifdef _WIN32
        if (IsUseDX12())
        {
            MSG message{};
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) throw std::runtime_error("Native window closed during integration test");
                TranslateMessage(&message); DispatchMessage(&message);
            }
        }
        else
#endif
        {
            auto* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
            if (!window || glfwWindowShouldClose(window)) throw std::runtime_error("GLFW window closed during integration test");
            glfwPollEvents();
        }
        report["frameCpuMilliseconds"].push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now()-frameStart).count());
    };
    const auto frames = [&](uint32_t count) { for (uint32_t i = 0; i < count; ++i) frame(); };
    try
    {
        if (!outputDirectory || !*outputDirectory) throw std::runtime_error("An explicit owned output directory is required");
        output = fs::absolute(fs::u8path(outputDirectory)); fs::create_directories(output);
        SetAuthoringTestStateDirectory(output / "workspace-state");
        if (!g_Engine || !g_Editor || !g_rhi || !ImGui::GetCurrentContext()) throw std::runtime_error("Test must run after real engine/RHI/UI initialization");
        auto& editor = *g_Editor;
        auto* camera = g_Engine->Game.MainCamera;
        if (!camera) throw std::runtime_error("Initialized engine has no main camera");
        report["rhi"] = g_rhi->GetRHIName().ToStr();
        report["inlineRaySupported"] = GSupportInlineRaytracing;
        const auto fixture = output / ("fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        jEditorProject project; std::string error;
        if (!project.Create(fixture, "GPU editor workflow fixture", error)) throw std::runtime_error(error);
        WriteModelFixture(fixture / "Assets");
        Document empty;
        if (!empty.Save(fixture / "Scenes/start.jscene")) throw std::runtime_error(empty.LastError());
        project.EditConfig().StartupScene = "Scenes/start.jscene";
        project.EditConfig().Settings["render.deferred"] = "true";
        project.EditConfig().Settings["render.vsync"] = "false";
        if (!project.Save(error)) throw std::runtime_error(error);
        expect(OpenAuthoringProject(editor, project.GetProjectFile(), error), "real project opens through transactional workspace lifecycle", error);
        if (!error.empty() && editor.GetProjectRoot() != project.GetRoot()) throw std::runtime_error(error);
        SetAuthoringWorkspaceVisible(true);
        SetTestPlatformWindows(true);
        SetAuthoringIntegrationDrawHook(RenderEditorIntegrationProbe);
        editor.SetAuthoringEnabled(true);
        auto& document = editor.GetDocument();
        if (!document.Entities().empty()) throw std::runtime_error("Fresh project must open with an empty authored document");
        std::vector<EntityId> ids;
        document.BeginTransaction("Create 100 GPU-backed authored entities");
        for (int i = 0; i < 100; ++i)
        {
            EntityKind kind = i % 2 ? EntityKind::Cube : EntityKind::Sphere;
            switch (i)
            {
            case 0: kind = EntityKind::Empty; break;
            case 1: kind = EntityKind::Camera; break;
            case 2: kind = EntityKind::DirectionalLight; break;
            case 3: kind = EntityKind::PointLight; break;
            case 4: kind = EntityKind::SpotLight; break;
            case 5: kind = EntityKind::Terrain; break;
            case 6: kind = EntityKind::ParticleEmitter; break;
            case 7: case 8: kind = EntityKind::Model; break;
            }
            const auto id = editor.CreateEntity(kind, std::string(KindName(kind)) + " " + std::to_string(i), i ? ids.front() : 0);
            if (!id) throw std::runtime_error(document.LastError());
            ids.push_back(id);
            auto entity = *document.Find(id);
            entity.Local.Position = i ? Vec3{float(i % 10) * 70 - 315, 20, float(i / 10) * 70 - 315} : Vec3{0, 5000, 0};
            entity.Local.RotationDegrees = {float(i % 3) * 7, float(i % 7) * 11, float(i % 4) * 5};
            if (i == 0) entity.Local.RotationDegrees = {};
            entity.Material.BaseColor = {0.25f + float(i % 5) * .15f, .4f, .75f};
            if (kind == EntityKind::Model)
            { entity.SourceAsset = "Assets/triangle.obj"; entity.Material.OverrideEnabled = false; entity.Local.Scale = {2, 2, 2}; }
            if (kind == EntityKind::Camera)
            { entity.Camera.Primary = true; entity.Local.Position = {0, 460, -800}; entity.Local.RotationDegrees = {25, 0, 0}; entity.Camera.FarPlane = 20000; }
            if (kind == EntityKind::DirectionalLight)
            { entity.Light.Intensity = 3; entity.Local.RotationDegrees = {25, 20, 20}; }
            if (kind == EntityKind::PointLight || kind == EntityKind::SpotLight)
            { entity.Local.Position.Y = 180; entity.Light.Range = 1200; entity.Light.Intensity = 5; entity.Light.CastShadows = true; }
            if (kind == EntityKind::Terrain)
            { entity.Terrain.Resolution = 9; entity.Terrain.SizeX = entity.Terrain.SizeZ = 800; entity.Local.Position = {0, -10, 0}; entity.Local.RotationDegrees = {}; entity.Material.BaseColor = {.3f,.4f,.2f}; }
            if (kind == EntityKind::ParticleEmitter)
            { entity.Particles.MaxParticles = 64; entity.Particles.SpawnRate = 30; entity.Particles.Lifetime = 2; entity.Particles.Color = {1,.35f,.1f}; entity.Material.OverrideEnabled = true; }
            if (i == 9)
            {
                entity.Animation.Enabled = true; entity.Animation.Loop = false; entity.Animation.Duration = 2;
                auto end = entity.Local; end.Position.X += 100;
                entity.Animation.Keys = {{0, entity.Local}, {2, end}};
            }
            if (!document.Update(entity, "Fixture properties")) throw std::runtime_error(document.LastError());
        }
        if (!document.CommitTransaction()) throw std::runtime_error(document.LastError());
        expect(document.Entities().size() == 100, "100 mixed entities enter one actual document transaction");
        jCamera::SetCamera(camera, Vector(0, 5480, -850), Vector(0, 5000, 0), Vector(0,5481,-850),
            DegreeToRadian(60), 1, 20000, SCR_WIDTH, SCR_HEIGHT, true, 980);
        camera->UpdateCamera(); editor.SelectEntity(ids[9]);
        Probe().Active = true;
        expect(jEditorUiRenderer::SupportsPlatformWindows(), "real platform and renderer advertise detached-window support");
        frames(30);
        while ((!editor.ObjectForEntity(ids[7]) || !editor.ObjectForEntity(ids[8]) || editor.PendingImportCount()) && rendered < 90) frame();
        size_t objectBindings = 0, lightBindings = 0, gpuMeshes = 0;
        for (const auto& [id, entity] : document.Entities())
        {
            (void)entity;
            if (auto* object = editor.ObjectForEntity(id))
            {
                ++objectBindings;
                for (auto* mesh : object->RenderObjects)
                    gpuMeshes += mesh && mesh->GeometryDataPtr && mesh->GeometryDataPtr->VertexBufferPtr
                        && mesh->GeometryDataPtr->VertexBufferPtr->GetBuffer(0);
            }
            lightBindings += editor.LightForEntity(id) != nullptr;
        }
        report["fixtureProject"] = project.GetProjectFile().generic_string();
        report["authoredEntities"] = document.Entities().size(); report["liveObjectBindings"] = objectBindings;
        report["liveLightBindings"] = lightBindings; report["liveGpuMeshes"] = gpuMeshes;
        expect(objectBindings == 95 && lightBindings == 3 && gpuMeshes >= 95, "all 100 typed entities publish actual owned object/light/GPU bindings", editor.LastError());
        expect(editor.PendingImportCount() == 0 && FirstMesh(editor, ids[7]) && FirstMesh(editor, ids[8]),
            "real OBJ and PNG import completes from CPU preparation through GPU admission", editor.ImportStatus());
        auto* modelA = FirstMesh(editor, ids[7]); auto* modelB = FirstMesh(editor, ids[8]);
        if (!modelA || !modelB) throw std::runtime_error("Actual imported model bindings were not published within 90 frames");
        expect(modelA != modelB && modelA->GeometryDataPtr == modelB->GeometryDataPtr
            && modelA->GeometryDataPtr->VertexBufferPtr == modelB->GeometryDataPtr->VertexBufferPtr
            && modelA->MaterialPtr == modelB->MaterialPtr, "model instances have independent identities and share actual geometry/texture material resources");
        auto* cubeA = FirstMesh(editor, ids[9]); auto* cubeB = FirstMesh(editor, ids[11]);
        expect(cubeA && cubeB && cubeA != cubeB && cubeA->GeometryDataPtr == cubeB->GeometryDataPtr,
            "repeated primitive instances share actual GPU geometry rather than reuploading each mesh");
        float matrix[16]{}; document.GetWorldMatrix(ids[9], matrix);
        expect(cubeA && MatrixNear(cubeA->World, Matrix(matrix)), "parented mixed-axis authored transform reaches actual render-object world matrix");
        const auto* point = editor.LightForEntity(ids[3]); const auto* spot = editor.LightForEntity(ids[4]);
        document.GetWorldMatrix(ids[3], matrix);
        expect(point && spot && point->GetPosition().IsNearlyEqual(Vector(matrix[12],matrix[13],matrix[14]))
            && spot->GetLightCamra(0), "actual authored point/spot lights expose positioned runtime and shadow-camera resources");
        auto originalMaterial = modelB->MaterialPtr;
        auto editedModel = *document.Find(ids[7]); editedModel.Material.OverrideEnabled = true;
        editedModel.Material.BaseColor = {.15f,.75f,.2f}; editedModel.Material.Metallic = .35f;
        editedModel.Material.Roughness = .65f; editedModel.Material.Emissive = {.01f,.02f,.01f};
        document.Update(editedModel, "Real instance material override"); frames(2);
        modelA = FirstMesh(editor, ids[7]); modelB = FirstMesh(editor, ids[8]);
        expect(modelA && modelB && modelA->MaterialPtr != originalMaterial && modelB->MaterialPtr == originalMaterial
            && modelA->GeometryDataPtr == modelB->GeometryDataPtr && modelA->MaterialPtr->UseMaterialProperties
            && std::abs(modelA->MaterialPtr->MetallicFactor-.35f)<.0001f && std::abs(modelA->MaterialPtr->RoughnessFactor-.65f)<.0001f,
            "material edit clones only the edited runtime material and retains shared geometry/original material");
        expect(modelA && modelA->MaterialPtr->ShaderBindingInstance,
            "authored override participates in real renderer descriptor publication after submitted frames");
        const auto snapshotPath = project.GetRoot() / "Scenes/100-entities.jscene";
        expect(document.Save(snapshotPath), "100 live entities save through production atomic persistence", document.LastError());
        const auto persisted = document.ToJson();
        editor.SelectEntity(ids[9]); const auto duplicate = editor.DuplicateSelected(); frames(2);
        expect(duplicate && document.Entities().size() == 101 && FirstMesh(editor, duplicate)
            && FirstMesh(editor, duplicate)->GeometryDataPtr == FirstMesh(editor, ids[9])->GeometryDataPtr,
            "duplicate command publishes independent live instance with shared GPU geometry");
        expect(editor.DeleteSelected(), "delete command accepts selected duplicate"); frames(2);
        expect(!document.Find(duplicate) && !editor.ObjectForEntity(duplicate), "delete retires actual runtime instance and clears authored ID");
        expect(document.Undo(), "Undo restores delete command"); frames(2);
        expect(document.Find(duplicate) && editor.ObjectForEntity(duplicate), "Undo reconstructs actual GPU-backed instance under its original stable ID");
        expect(document.Redo(), "Redo reapplies delete command"); frames(2);
        expect(document.Entities().size() == 100 && !editor.ObjectForEntity(duplicate), "Redo removes reconstructed instance without stale world bindings");
        expect(LoadAuthoringScene(editor, snapshotPath, error), "workspace reopens the saved authored scene transactionally", error); frames(3);
        expect(document.ToJson() == persisted && document.Entities().size() == 100 && editor.ObjectForEntity(ids[7]) && editor.LightForEntity(ids[4]),
            "save/reopen preserves all stable IDs/properties and rebuilds real model/light bindings");
        const auto prefabPath = project.GetRoot() / "Assets/cube.jprefab";
        expect(document.SavePrefab(ids[9], prefabPath), "prefab stores selected real authored instance");
        const auto prefab = document.InstantiatePrefab(prefabPath, ids[0]); frames(2);
        expect(prefab && editor.ObjectForEntity(prefab) && document.Entities().size() == 101,
            "prefab instantiation creates a real renderer instance");
        expect(document.Undo(), "prefab instantiate supports Undo"); frames(2);
        expect(!editor.ObjectForEntity(prefab) && document.Entities().size() == 100, "prefab Undo retires real instance");
        editor.SelectEntity(ids[9]);
        const auto editSnapshot = document.ToJson(); const auto editCamera = camera->Pos;
        const auto editCameraTarget = camera->Target, editCameraUp = camera->Up;
        const float editFov = camera->FOVRad, editDistance = camera->Distance;
        expect(editor.BeginPlay() && editor.SetPlayTime(1), "Play begins from real edit-state snapshot and timeline seek"); frames(3);
        Transform sampled; document.EvaluateLocal(ids[9], editor.PlayTime(), sampled);
        float parentMatrix[16]; document.GetWorldMatrix(ids[0], parentMatrix);
        const auto* animated = FirstMesh(editor, ids[9]);
        const Vector expectedPosition = Matrix(parentMatrix).TransformPoint(Vector(sampled.Position.X,sampled.Position.Y,sampled.Position.Z));
        expect(animated && Vector(animated->World.m30,animated->World.m31,animated->World.m32).IsNearlyEqual(expectedPosition, .01f)
            && document.ToJson() == editSnapshot, "Play sampling moves the actual GPU-rendered world without editing authored data");
        expect(!camera->Pos.IsNearlyEqual(editCamera), "Play activates authored primary camera in the actual renderer");
        const auto temporary = editor.CreateEntity(EntityKind::Cube, "Play-only runtime entity", ids[0]); frames(2);
        expect(temporary && editor.ObjectForEntity(temporary), "Play-only entity actually publishes live geometry");
        expect(editor.StopPlay(), "Stop restores edit snapshot"); frames(3);
        expect(!document.Find(temporary) && !editor.ObjectForEntity(temporary) && document.ToJson() == editSnapshot
            && camera->Pos.IsNearlyEqual(editCamera) && camera->Target.IsNearlyEqual(editCameraTarget) && camera->Up.IsNearlyEqual(editCameraUp)
            && std::abs(camera->FOVRad-editFov)<.0001f && std::abs(camera->Distance-editDistance)<.001f,
            "Stop removes ephemeral GPU instance and restores exact edit scene/camera state");
        auto* oldTerrain = FirstMesh(editor, ids[5]);
        auto oldGeometry = oldTerrain ? oldTerrain->GeometryDataPtr : nullptr;
        auto terrain = *document.Find(ids[5]);
        expect(jEditorTools::PaintTerrain(terrain.Terrain, 0, 0, 220, 35, 0) && document.Update(terrain, "GPU terrain brush"),
            "terrain brush edits the actual authored height grid"); frames(3);
        auto* newTerrain = FirstMesh(editor, ids[5]);
        expect(newTerrain && newTerrain->GeometryDataPtr && newTerrain->GeometryDataPtr != oldGeometry,
            "terrain edit replaces actual uploaded geometry generation at frame-safe boundary");
        bool readback = false;
        if (newTerrain && newTerrain->GeometryDataPtr && newTerrain->GeometryDataPtr->VertexBufferPtr
            && newTerrain->GeometryDataPtr->VertexStreamPtr && !newTerrain->GeometryDataPtr->VertexStreamPtr->Params.empty())
            readback = BufferMatches(newTerrain->GeometryDataPtr->VertexBufferPtr->GetBuffer(0), *newTerrain->GeometryDataPtr->VertexStreamPtr->Params.front());
        expect(readback, "painted terrain GPU vertex bytes match the actual rebuilt CPU vertex stream");
        expect(document.Undo(), "terrain brush undo command accepted"); frames(3);
        expect(document.Find(ids[5]) && document.Find(ids[5])->Terrain.Heights.empty() && FirstMesh(editor, ids[5]),
            "terrain Undo restores flat data and a live geometry generation");
        if (rendered < 144) frames(144-rendered);
        expect(rendered >= 144 && rendered <= 240, "full initialized engine submits at least 144 bounded editor frames");
        expect(Probe().MaximumViewports > 1 && Probe().SecondaryRenderedFrames > 5 && Probe().DetachedId,
            "detached ImGui window has a real OS handle renderer data and multiple submitted platform frames");
        report["maximumPlatformViewports"] = Probe().MaximumViewports;
        report["secondaryRenderedFrames"] = Probe().SecondaryRenderedFrames;
        report["materialUboReadback"] = "Not exposed publicly; checked live COW properties and actual descriptor publication.";
        Probe().Active = false; frames(4); g_rhi->Flush();
        expect(ImGui::GetPlatformIO().Viewports.Size <= 1, "closing detached probe drains and destroys secondary renderer/OS viewport resources");
        document.Save(snapshotPath);
        report["finalScene"] = snapshotPath.generic_string();
    }
    catch (const std::exception& exception)
    {
        ++failures;
        report["exception"] = exception.what();
        std::cerr << "FAIL editor GPU flow exception: " << exception.what() << std::endl;
    }
    Probe().Active = false;
    SetAuthoringIntegrationDrawHook({});
    SetTestPlatformWindows(false);
    SetAuthoringWorkspaceVisible(oldWorkspace);
    g_KeyState = oldKeys; g_MouseState = oldMouse;
    report["renderedFrames"] = rendered; report["failureCount"] = failures;
    report["status"] = failures ? "failed" : "passed";
    try
    {
        if (!output.empty()) Write(output / "editor-authoring-gpu-report.json", report.dump(2) + "\n");
    }
    catch (const std::exception& exception) { std::cerr << "Cannot save GPU test report: " << exception.what() << std::endl; ++failures; }
    std::cout << "Editor authoring GPU workflow: " << rendered << " actual frames, " << failures << " failures" << std::endl;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
#else
    (void)outputDirectory;
    std::cerr << "Editor GPU workflow requires ENABLE_EDITOR_FEATURES" << std::endl;
    return EXIT_FAILURE;
#endif
}
