#include "pch.h"
#include "jEditorIntegrationTests.h"
#include "jViewportToolbarTests.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Code/Engine/Editor/jEditorUiRenderer.h"
#include "Code/Engine/Editor/jEditorCapture.h"
#include "FileLoader/jEditorAssets.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"
#include "Material/jMaterial.h"
#include "RHI/Vulkan/jBufferUtil_Vulkan.h"
#include "RHI/jRaytracingScene.h"
#include "External/IMGUI/imgui_internal.h"
#ifdef _WIN32
#include "RHI/DX12/jBufferUtil_DX12.h"
#include "RHI/DX12/jTexture_DX12.h"
#endif
#include "jEngine.h"
#include "jOptions.h"
#include "lodepng.h"
#include "stb_image.h"
#include <set>
#include "External/PathTracingDataLoader/json.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <limits>

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
    Write(assets / "pick-parts.obj", "mtllib triangle.mtl\n"
        "v 45 0 0\nv 75 0 0\nv 60 30 0\nv -15 0 0\nv 15 0 0\nv 0 30 0\n"
        "vt 0 0\nvt 1 0\nvt 0.5 1\nvn 0 0 -1\n"
        "o Side\nusemtl fixture\nf 1/1/1 3/3/1 2/2/1\n"
        "o Center\nusemtl fixture\nf 4/1/1 6/3/1 5/2/1\n");
    Write(assets / "triangle.mtl", "newmtl fixture\nKd 1 1 1\nmap_Kd color.png\n");
    std::array<unsigned char, 4 * 4 * 4> red{};
    for (size_t i = 0; i < red.size(); i += 4) red[i] = red[i + 3] = 255;
    unsigned char* encoded = nullptr;
    size_t size = 0;
    const auto encodeError = LodePNG_encode32(&encoded, &size, red.data(), 4, 4);
    std::unique_ptr<unsigned char, decltype(&std::free)> png(encoded, std::free);
    if (encodeError) throw std::runtime_error("Could not encode model fixture texture");
    std::ofstream output(assets / "color.png", std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.write(reinterpret_cast<const char*>(png.get()), static_cast<std::streamsize>(size));
}
jRenderObject* FirstMesh(jEditor& editor, EntityId id)
{
    auto* object = editor.ObjectForEntity(id);
    return object && !object->RenderObjects.empty() ? object->RenderObjects.front() : nullptr;
}
bool IsOutlinePixel(const uint8* pixel)
{
    return pixel[0] >= 250 && pixel[3] >= 254 &&
        ((pixel[1] >= 160 && pixel[1] <= 166 && pixel[2] >= 37 && pixel[2] <= 45) ||
         (pixel[1] >= 205 && pixel[1] <= 212 && pixel[2] >= 107 && pixel[2] <= 116));
}
std::string ReadCaptureFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
bool CheckStill(const fs::path& path, int width, int height, bool checkScene)
{
#ifdef _WIN32
    FILE* handle = nullptr;
    if (_wfopen_s(&handle, path.c_str(), L"rb") != 0) return false;
#else
    FILE* handle = std::fopen(path.c_str(), "rb");
#endif
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(handle, std::fclose);
    if (!file) return false;
    int w = 0, h = 0, channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image(
        stbi_load_from_file(file.get(), &w, &h, &channels, 4),
        stbi_image_free);
    if (!image || w != width || h != height) return false;
    if (!checkScene) return true;
    size_t lit = 0, outline = 0, opaque = 0;
    for (size_t index = 0; index < size_t(w) * h; ++index)
    {
        const auto* pixel = image.get() + index * 4;
        lit += std::max({pixel[0], pixel[1], pixel[2]}) > 12;
        opaque += pixel[3] == 255;
        outline += IsOutlinePixel(pixel);
    }
    return lit > 12 && opaque == size_t(w) * h && outline == 0;
}
bool CheckMp4(const fs::path& path, unsigned width, unsigned height)
{
    const auto file = ReadCaptureFile(path);
    const auto table = file.find("stsd");
    const auto avc = file.find("avc1", table);
    if (file.size() < 32 || file.compare(4, 4, "ftyp") != 0 || file.find("mdat") == std::string::npos
        || avc == std::string::npos || avc + 32 >= file.size() || file.find("avcC", avc) == std::string::npos) return false;
    const auto u16 = [&](size_t offset) { return (unsigned(uint8(file[offset])) << 8) | unsigned(uint8(file[offset + 1])); };
    return u16(avc + 28) == width && u16(avc + 30) == height;
}
Json ReadScenePixels(const std::shared_ptr<jTexture>& texture, const fs::path& png, const jCamera* camera, const Vector& floorPoint)
{
    Json result{{"available", bool(texture)}, {"readback", false}};
    if (!texture || !texture->Width || !texture->Height || texture->Width > 4096 || texture->Height > 4096
        || (texture->Format != ETextureFormat::RGBA8 && texture->Format != ETextureFormat::BGRA8
            && !(IsUseDX12() && texture->Format == ETextureFormat::RGB8))) return result;
    const uint32 width = texture->Width, height = texture->Height;
    uint32 rowPitch = width * 4;
    uint64 size = uint64(rowPitch) * height;
#ifdef _WIN32
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    if (!IsUseVulkan())
    {
        const auto description = static_cast<jTexture_DX12*>(texture.get())->Texture->Get()->GetDesc();
        uint32 rows = 0; uint64 rowBytes = 0;
        g_rhi_dx12->Device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, &rows, &rowBytes, &size);
        rowPitch = footprint.Footprint.RowPitch;
    }
#endif
    g_rhi->Flush();
    auto readback = g_rhi->CreateRawBuffer(size, 0, EBufferCreateFlag::Readback,
        IsUseVulkan() ? EResourceLayout::UNDEFINED : EResourceLayout::TRANSFER_DST);
    if (!readback || !readback->GetHandle()) return result;
    auto* command = g_rhi->BeginSingleTimeCommands();
    if (!command) return result;
    const auto previous = texture->GetLayout();
    g_rhi->TransitionLayout(command, texture.get(), EResourceLayout::TRANSFER_SRC);
    g_rhi->TransitionLayout(command, readback.get(), EResourceLayout::TRANSFER_DST);
    command->FlushBarrierBatch();
    if (IsUseVulkan())
    {
        const auto native = static_cast<jCommandBuffer_Vulkan*>(command)->GetRef();
        VkBufferImageCopy copy{}; copy.bufferOffset = readback->GetOffset();
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width,height,1};
        vkCmdCopyImageToBuffer(native, static_cast<jTexture_Vulkan*>(texture.get())->Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, static_cast<jBuffer_Vulkan*>(readback.get())->Buffer, 1, &copy);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    }
#ifdef _WIN32
    else
    {
        D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
        source.pResource = static_cast<ID3D12Resource*>(texture->GetHandle()); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource = static_cast<ID3D12Resource*>(readback->GetHandle());
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
        static_cast<jCommandBuffer_DX12*>(command)->Get()->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
    }
#endif
    g_rhi->TransitionLayout(command,texture.get(),previous); command->FlushBarrierBatch();
    g_rhi->EndSingleTimeCommands(command);
    const auto* mapped = static_cast<const uint8*>(readback->Map());
    if (!mapped) return result;
    std::vector<uint8> rgba(size_t(width)*height*4);
    std::set<uint32> colors;
    size_t lit = 0, opaque = 0, outline = 0; int minimum = 255, maximum = 0;
    uint64 pixelHash = 1469598103934665603ull;
    std::array<uint64, 3> channelSum{};
    for (uint32 y=0;y<height;++y) for (uint32 x=0;x<width;++x)
    {
        const auto* pixel = mapped+size_t(y)*rowPitch+x*4;
        auto* output = rgba.data()+(size_t(y)*width+x)*4;
        output[0]=pixel[texture->Format==ETextureFormat::BGRA8?2:0];output[1]=pixel[1];
        output[2]=pixel[texture->Format==ETextureFormat::BGRA8?0:2];output[3]=pixel[3];
        for (int channel = 0; channel < 4; ++channel) { pixelHash ^= output[channel]; pixelHash *= 1099511628211ull; }
        for (int channel = 0; channel < 3; ++channel) channelSum[channel] += output[channel];
        const int brightest=std::max({int(output[0]),int(output[1]),int(output[2])});
        const int darkest=std::min({int(output[0]),int(output[1]),int(output[2])});
        lit+=brightest>12; opaque+=output[3]>=254;
        outline += IsOutlinePixel(output);
        minimum=std::min(minimum,darkest);maximum=std::max(maximum,brightest);
        if(colors.size()<1024)colors.insert(uint32(output[0])|(uint32(output[1])<<8)|(uint32(output[2])<<16));
    }
    if (camera)
    {
        const auto clip=camera->ViewProjection.Transform(Vector4(floorPoint,1));
        const bool inside=clip.w>0 && std::abs(clip.x/clip.w)<1 && std::abs(clip.y/clip.w)<1;
        size_t floorLit=0,floorSamples=0;
        int px=-1,py=-1;
        if (inside)
        {
            px=int((clip.x/clip.w*.5f+.5f)*width);
            py=int((.5f-clip.y/clip.w*.5f)*height);
            for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx)
            {
                const int x=px+dx,y=py+dy;
                if(x<0||y<0||x>=int(width)||y>=int(height))continue;
                const auto* pixel=rgba.data()+(size_t(y)*width+size_t(x))*4;
                ++floorSamples;floorLit+=std::max({pixel[0],pixel[1],pixel[2]})>20;
            }
        }
        result["floorProbe"]={{"inside",inside},{"pixelX",px},{"pixelY",py},
            {"world",{floorPoint.x,floorPoint.y,floorPoint.z}},{"samples",floorSamples},{"litPixels",floorLit}};
    }
    readback->Unmap();
    const bool pngSaved = !png.empty() && LodePNG_encode32_file(png.string().c_str(), rgba.data(), width, height) == 0;
    result.update({{"readback",true},{"width",width},{"height",height},{"litPixels",lit},{"opaquePixels",opaque},
        {"distinctColorsAtMost1024",colors.size()},{"minimumRgb",minimum},{"maximumRgb",maximum},
        {"outlinePixels",outline},{"pngSaved",pngSaved},{"png",png.generic_string()},{"pixelHash",std::to_string(pixelHash)},
        {"meanRgb",{double(channelSum[0])/(size_t(width)*height),double(channelSum[1])/(size_t(width)*height),double(channelSum[2])/(size_t(width)*height)}}});
    return result;
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
    const auto toggleViewportOnly = [&] {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_F11, true);
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_F11, false);
        frame();
    };
    try
    {
        if (!outputDirectory || !*outputDirectory) throw std::runtime_error("An explicit owned output directory is required");
        output = fs::absolute(jEditorAssetPath::FromUtf8(outputDirectory)); fs::create_directories(output);
        SetAuthoringTestStateDirectory(output / "workspace-state");
        if (!g_Engine || !g_Editor || !g_rhi || !ImGui::GetCurrentContext()) throw std::runtime_error("Test must run after real engine/RHI/UI initialization");
        auto& editor = *g_Editor;
        auto* camera = g_Engine->Game.MainCamera;
        if (!camera) throw std::runtime_error("Initialized engine has no main camera");
        report["rhi"] = g_rhi->GetRHIName().ToStr();
        report["inlineRaySupported"] = GSupportInlineRaytracing;
        frame();
        SetAuthoringToolsVisible(true);
        frames(3);
        auto* tools = ImGui::FindWindowByName("Authoring tools");
        auto* toolsViewport = tools ? tools->Viewport : nullptr;
        const auto* mainViewport = ImGui::GetMainViewport();
        const auto* sceneWindow = ImGui::FindWindowByName("Scene");
        const bool detached = toolsViewport && toolsViewport != mainViewport && toolsViewport->PlatformHandle
            && toolsViewport->RendererUserData && toolsViewport->DrawData && toolsViewport->DrawData->TotalVtxCount > 0;
        expect(detached && sceneWindow && sceneWindow->Viewport == mainViewport,
            "Authoring tools renders in its own native window while Scene stays in the main window");
        if (detached)
        {
            const auto viewportId = toolsViewport->ID;
            toolsViewport->PlatformRequestClose = true;
            frames(3);
            const auto* closedViewport = ImGui::FindViewportByID(viewportId);
            expect((!closedViewport || (!closedViewport->PlatformHandle && !closedViewport->RendererUserData))
                && ImGui::GetMainViewport()->PlatformHandle,
                "closing the Authoring tools native window keeps the main editor open");
        }
        SetAuthoringToolsVisible(false);
        const auto& startupEntities = editor.GetDocument().Entities();
        if (std::any_of(startupEntities.begin(), startupEntities.end(), [](const auto& entry) {
            return entry.second.SourceAsset.ends_with("sponza_pbr/sponza.glb");
        }))
        {
            const auto sphere = std::find_if(startupEntities.begin(), startupEntities.end(), [](const auto& entry) {
                return entry.second.Kind == EntityKind::Sphere && entry.second.Local.Position == Vec3{65, 35, 10};
            });
            const auto id = sphere == startupEntities.end() ? InvalidEntity : sphere->first;
            const auto* object = editor.ObjectForEntity(id);
            expect(object && sphere->second.Local.Scale == Vec3{2, 2, 2}, "Sponza preset sphere appears in the document at its original size");
            jCamera::SetCamera(camera, Vector(155,35,10), Vector(65,35,10), Vector(155,36,10),
                DegreeToRadian(60), 1, 5000, SCR_WIDTH, SCR_HEIGHT, true, 90);
            camera->UpdateCamera();
            const auto pickSphere = [&] {
                editor.Placement.EnablePlacementMode = true;
                editor.Placement.bPickRequested = true;
                editor.Placement.PickMouseX = SCR_WIDTH / 2;
                editor.Placement.PickMouseY = SCR_HEIGHT / 2;
                frame();
            };
            pickSphere();
            const auto sphereWorld = Matrix::MakeTranslate(Vector(65, 35, 10)) * Matrix::MakeScale(Vector(30.0f));
            expect(object && !object->RenderObjects.empty() && MatrixNear(object->RenderObjects.front()->World, sphereWorld)
                && editor.GetSelectedEntityId() == id && editor.GetSelectionRenderObjects() == object->RenderObjects,
                "GPU picking selects the Sponza preset sphere in the Hierarchy at its original world size");
            SetAuthoringWorkspaceVisible(false);
            pickSphere();
            expect(object && editor.Placement.PickedObject == object && editor.Placement.SelectedPlacedObjectIndex >= 0,
                "F10 Placement picks the same Sponza preset sphere");
            SetAuthoringWorkspaceVisible(true);
        }
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
        const auto front = document.Create(EntityKind::Cube, "Front selection target");
        const auto back = document.Create(EntityKind::Cube, "Overlapping selection target");
        document.SetLocalPositions({{front, {0, 1000, 0}}, {back, {0, 1000, 45}}}, "Overlap selection targets");
        jCamera::SetCamera(camera, Vector(0,1000,-200), Vector(0,1000,0), Vector(0,1001,-200),
            DegreeToRadian(60), 1, 20000, SCR_WIDTH, SCR_HEIGHT, true, 200);
        camera->UpdateCamera();
        editor.SelectEntity(0);
        frames(2);
        const auto beforeOutline = ReadScenePixels(jEditorUiRenderer::AcquireScenePreview(), {}, nullptr, Vector::ZeroVector);
        editor.Placement.bPickRequested = true;
        editor.Placement.PickMouseX = SCR_WIDTH / 2;
        editor.Placement.PickMouseY = SCR_HEIGHT / 2;
        frame();
        expect(editor.GetSelectedEntities() == std::vector<EntityId>{front} && document.SelectedId() == front,
            "GPU Scene picking selects the nearest overlapping authored mesh");
        const auto withOutline = ReadScenePixels(jEditorUiRenderer::AcquireScenePreview(), {}, nullptr, Vector::ZeroVector);
        expect(beforeOutline.value("readback", false) && withOutline.value("readback", false) &&
            withOutline.value("outlinePixels", size_t(0)) > beforeOutline.value("outlinePixels", size_t(0)) + 12,
            "selected mesh renders an amber contour into the real Scene preview", withOutline.dump());
        editor.Placement.bPickRequested = true;
        editor.Placement.PickToggleSelection = true;
        frame();
        expect(editor.GetSelectedEntities().empty() && !document.SelectedId(), "Ctrl Scene picking toggles the mesh out of selection");
        const auto pickModel = editor.AddModel("Assets/pick-parts.obj", {}, 0, Vec3{0, 1200, 0});
        jCamera::SetCamera(camera, Vector(0,1210,-200), Vector(0,1210,0), Vector(0,1211,-200),
            DegreeToRadian(60), 1, 20000, SCR_WIDTH, SCR_HEIGHT, true, 200);
        camera->UpdateCamera();
        const uint32_t pickDeadline = rendered + 20;
        while (pickModel && !editor.ObjectForEntity(pickModel) && rendered < pickDeadline) frame();
        const auto* modelObject = editor.ObjectForEntity(pickModel);
        editor.Placement.bPickRequested = true;
        editor.Placement.PickMouseX = SCR_WIDTH / 2;
        editor.Placement.PickMouseY = SCR_HEIGHT / 2;
        frame();
        const auto pickedSubmesh = editor.GetSelectedModelSubmesh();
        const auto pickedOutline = editor.GetSelectionRenderObjects();
        expect(modelObject && modelObject->RenderObjects.size() == 2 && pickedSubmesh && pickedSubmesh->Model == pickModel
            && pickedSubmesh->Index == 1 && pickedOutline == std::vector<jRenderObject*>{modelObject->RenderObjects[1]},
            "GPU Scene picking preserves the exact imported submesh and outlines only that part");
        if (!LoadAuthoringScene(editor, fixture / "Scenes/start.jscene", error)) throw std::runtime_error(error);
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
            { entity.Terrain.Resolution = 9; entity.Terrain.SizeX = entity.Terrain.SizeZ = 800; entity.Local.Position = {0, -10, 0}; entity.Local.RotationDegrees = {}; entity.Material.BaseColor = {.3f,.4f,.2f}; entity.Material.Emissive = {.04f,.08f,.03f}; }
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
        const auto centerClip=camera->ViewProjection.Transform(Vector4(0,5000,0,1));
        report["fixtureCamera"]={{"position",{camera->Pos.x,camera->Pos.y,camera->Pos.z}},
            {"target",{camera->Target.x,camera->Target.y,camera->Target.z}},
            {"centerClip",{centerClip.x,centerClip.y,centerClip.z,centerClip.w}}};
        expect(centerClip.w>0 && std::abs(centerClip.x/centerClip.w)<1 && std::abs(centerClip.y/centerClip.w)<1
            && centerClip.z/centerClip.w>=0 && centerClip.z/centerClip.w<=1,
            "fixture camera projects authored grid center inside the actual view volume");
        auto scenePixels=ReadScenePixels(jEditorUiRenderer::AcquireScenePreview(),output/"scene-preview-before-ui.png",camera,Vector(380,4990,-380));
        report["preUiScenePixels"]=scenePixels;
        expect(scenePixels.value("available",false)&&scenePixels.value("readback",false),
            "actual pre-UI scene preview texture exists and supports independent GPU readback");
        const uint64 totalScenePixels=uint64(scenePixels.value("width",0u))*scenePixels.value("height",0u);
        expect(totalScenePixels && scenePixels.value("litPixels",size_t(0))>std::max<size_t>(64,size_t(totalScenePixels/1000))
            && scenePixels.value("distinctColorsAtMost1024",size_t(0))>=8,
            "GPU scene pixels contain visible varied geometry before any UI gizmo or thumbnail overlays");
        expect(totalScenePixels && scenePixels.value("opaquePixels",size_t(0))>=totalScenePixels*99/100,
            "scene preview pixels are opaque so ImGui compositing cannot hide the rendered world");
        const auto floorProbe=scenePixels.value("floorProbe",Json::object());
        expect(floorProbe.value("inside",false)&&floorProbe.value("samples",size_t(0))>=40
            &&floorProbe.value("litPixels",size_t(0))*4>=floorProbe.value("samples",size_t(0))*3,
            "terrain floor is actually rasterized from above at an unoccluded pre-UI pixel patch");
        {
            using namespace jEditorCapture;
            const auto finishCapture = [&] {
                for (int i = 0; i < 8 && GetStatus().State == Phase::Screenshot; ++i) frame();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (GetStatus().State == Phase::Saving && std::chrono::steady_clock::now() < deadline)
                {
                    EndFrame();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                return GetStatus().State == Phase::Idle && GetStatus().Error.empty();
            };
            const auto png = output / jEditorAssetPath::FromUtf8("capture-장면.png");
            const bool screenshot = Start({png, 341, 223, 30, Format::Png}, error) && finishCapture();
            expect(screenshot && CheckStill(png, 341, 223, true),
                "PNG screenshot decodes at a custom resolution with scene pixels and without selection guides",
                error.empty() ? GetStatus().Error : error);
            expect(!Start({png, 341, 223, 30, Format::Png}, error) && CheckStill(png, 341, 223, true),
                "capture rejects an existing output filename and leaves its pixels intact");
            for (const auto format : {Format::Jpeg, Format::Bmp})
            {
                if (!Supports(format)) continue;
                auto path = output / "capture-format";
                path.replace_extension(Extension(format));
                const bool saved = Start({path, 246, 154, 30, format}, error) && finishCapture();
                expect(saved && CheckStill(path, 246, 154, false),
                    format == Format::Jpeg ? "JPEG capture decodes at the requested size" : "BMP capture decodes at the requested size",
                    error.empty() ? GetStatus().Error : error);
            }

            const auto selection = editor.GetSelectedEntityId();
            const float sceneAspect = GetAuthoringSceneAspect();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_F11, true);
            frames(3);
            const auto* main = ImGui::GetMainViewport();
            const auto& platformWindows = ImGui::GetPlatformIO().Viewports;
            const bool detachedVisible = std::any_of(platformWindows.begin(), platformWindows.end(),
                [&](const auto* viewport) { return viewport != main && viewport->PlatformHandle; });
            expect(IsAuthoringViewportOnly() && camera->ProjectionAspectOverride == 0.f
                && ImGui::GetDrawData()->TotalVtxCount == 0 && !detachedVisible,
                "F11 presents the full window without editor draws or detached panels and stays active while held");
            ImGui::GetIO().AddKeyEvent(ImGuiKey_F11, false);
            frame();
            const auto fullPng = output / "capture-viewport-only.png";
            const bool fullCapture = Start({fullPng, 320, 180, 30, Format::Png}, error) && finishCapture();
            expect(fullCapture && CheckStill(fullPng, 320, 180, true) && IsAuthoringViewportOnly(),
                "viewport-only mode still renders and captures the scene without editor overlays",
                error.empty() ? GetStatus().Error : error);
            toggleViewportOnly();
            frames(2);
            expect(!IsAuthoringViewportOnly() && IsAuthoringWorkspaceVisible() && editor.GetSelectedEntityId() == selection
                && sceneAspect > 0.f && std::abs(GetAuthoringSceneAspect() - sceneAspect) < .001f
                && ImGui::GetDrawData()->TotalVtxCount > 0,
                "F11 restores the Scene layout, aspect ratio, and selection");

            if (Supports(Format::Mp4))
            {
                const auto path = output / "capture-video.mp4";
                expect(!Start({path, 1281, 720, 30, Format::Mp4}, error) && !fs::exists(path),
                    "MP4 capture rejects odd dimensions before creating a file");
                const bool started = Start({path, 1280, 720, 30, Format::Mp4}, error);
                bool recordedInViewportOnly = false;
                if (started)
                {
                    frames(5);
                    toggleViewportOnly();
                    std::this_thread::sleep_for(std::chrono::milliseconds(90));
                    frames(5);
                    recordedInViewportOnly = IsAuthoringViewportOnly() && GetStatus().State == Phase::Recording;
                    toggleViewportOnly();
                    Stop();
                    frame();
                }
                expect(started && finishCapture() && recordedInViewportOnly && GetStatus().Frames >= 2 && CheckMp4(path, 1280, 720),
                    "recording continues across F11 transitions and finalizes a 720p H.264 MP4",
                    error.empty() ? GetStatus().Error : error);
            }
        }
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
        toggleViewportOnly();
        expect(IsAuthoringViewportOnly() && document.IsPlaying() && ImGui::GetDrawData()->TotalVtxCount == 0,
            "F11 fills the window with the Game view during Play");
        toggleViewportOnly();
        expect(!IsAuthoringViewportOnly() && document.IsPlaying() && ImGui::GetDrawData()->TotalVtxCount > 0,
            "F11 restores the Game layout while Play continues");
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

int RunRendererSwitchGpuTests(const char* outputDirectory)
{
#ifdef ENABLE_EDITOR_FEATURES
    using Pipeline = jGame::ESceneRenderPipeline;
    Json report{{"checks", Json::array()}, {"captures", Json::array()}};
    fs::path output;
    int failures = 0;
    uint32 rendered = 0;
    const bool oldWorkspace = IsAuthoringWorkspaceVisible();
    const auto oldKeys = g_KeyState;
    const auto oldMouse = g_MouseState;
    const auto expect = [&](bool pass, const char* name) {
        report["checks"].push_back({{"name", name}, {"pass", pass}, {"frame", rendered}});
        std::cout << (pass ? "PASS " : "FAIL ") << "renderer switch: " << name << std::endl;
        failures += !pass;
    };
    const auto frame = [&] {
        if (++rendered > 180) throw std::runtime_error("Renderer switch test exceeded 180 frames");
        ResetAllKeyState(); ResetMouseAllState();
        g_timeDeltaSecond = 1.f / 60.f;
        g_Engine->ProcessInput(g_timeDeltaSecond);
        g_rhi->IncrementFrameNumber();
        g_Engine->Update(g_timeDeltaSecond);
        g_Engine->Draw(); g_Engine->EndOfFrame();
#ifdef _WIN32
        if (IsUseDX12())
        {
            MSG message{};
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
            { TranslateMessage(&message); DispatchMessage(&message); }
        }
        else
#endif
            glfwPollEvents();
    };
    const auto frames = [&](uint32 count) { while (count--) frame(); };
    const auto moveMouse = [&](const ImVec2& point) {
        if (IsUseVulkan())
        {
            auto* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
            int x = 0, y = 0;
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) glfwGetWindowPos(window, &x, &y);
            glfwSetCursorPos(window, point.x - x, point.y - y);
        }
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
    };
    const auto click = [&](const ImVec2& point) {
        moveMouse(point); frame();
        ImGui::GetIO().AddMouseButtonEvent(0, true); frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false); frame();
    };
    const auto popup = [&]() -> ImGuiWindow* {
        auto* context = ImGui::GetCurrentContext();
        if (!context || context->OpenPopupStack.empty()) return nullptr;
        return context->OpenPopupStack.back().Window;
    };
    const auto openCombo = [&] {
        float x = 0, y = 0, width = 0, height = 0;
        if (!GetAuthoringRendererControlRect(x, y, width, height))
            throw std::runtime_error("Viewport renderer control is not visible");
        click(ImVec2(x + width * .5f, y + height * .5f));
        // Combo popups use their opening frame to calculate the automatic size.
        frame();
        if (!popup()) throw std::runtime_error("Actual renderer combo click did not open its popup");
    };
    try
    {
        if (!outputDirectory || !*outputDirectory || !g_Engine || !g_Editor || !g_rhi || !ImGui::GetCurrentContext())
            throw std::runtime_error("Renderer switch test requires initialized engine/UI and an owned output directory");
        output = fs::absolute(jEditorAssetPath::FromUtf8(outputDirectory)); fs::create_directories(output);
        auto& game = g_Engine->Game;
        auto& editor = *g_Editor;
        SetAuthoringWorkspaceVisible(true); frames(3);
        std::string reason;
        expect(!game.RequestSceneRenderPipeline(static_cast<Pipeline>(999), reason) && !reason.empty(),
            "invalid renderer request is rejected with a reason");
        expect(game.RequestSceneRenderPipeline(Pipeline::Deferred, reason), "initial Deferred request is accepted");
        frames(3);
        if (game.GetActiveSceneRenderPipeline() != Pipeline::Deferred) throw std::runtime_error("Cannot enter initial Deferred renderer");
        auto* camera = game.MainCamera;
        if (!camera) throw std::runtime_error("Camera is missing");
        const auto cameraPosition = camera->Pos, cameraTarget = camera->Target, cameraUp = camera->Up;
        const float fov = camera->FOVRad;
        const auto selection = editor.GetSelectedEntities();
        const auto document = editor.GetDocument().ToJson();
        const bool dirty = editor.GetDocument().IsDirty();
        const auto objects = jObject::GetStaticRenderObject();
        std::vector<jRenderObjectGeometryData*> geometry;
        std::vector<jMaterial*> materials;
        for (const auto* object : objects)
        { geometry.push_back(object->GeometryDataPtr.get()); materials.push_back(object->MaterialPtr.get()); }
        const bool pathSupported = game.CanSwitchSceneRenderPipeline(Pipeline::PathTracing, reason);
        report["pathTracingSupported"] = pathSupported;
        report["pathTracingReason"] = reason;
        if (!pathSupported)
            expect(!game.RequestSceneRenderPipeline(Pipeline::PathTracing, reason) && !reason.empty()
                && game.GetActiveSceneRenderPipeline() == Pipeline::Deferred,
                "unsupported path tracing keeps the current renderer");
        const Pipeline targets[] = {Pipeline::PathTracing, Pipeline::Deferred, Pipeline::Forward, Pipeline::PathTracing};
        uint32 step = 0;
        for (const auto target : targets)
        {
            if (target == Pipeline::PathTracing && !pathSupported) continue;
            const auto before = game.GetActiveSceneRenderPipeline();
            if (before == target) continue;
            const auto previousPathOutput = g_rhi->RaytracingScene
                ? g_rhi->RaytracingScene->RaytracingOutputPtr : std::shared_ptr<jTexture>{};
            openCombo();
            expect(!IsAuthoringViewportHovered() && !IsAuthoringViewportFocused(),
                "renderer toolbar popup does not own scene camera or picking input");
            float x = 0, y = 0, width = 0, height = 0;
            if (!GetAuthoringRendererChoiceRect(static_cast<int>(target), x, y, width, height))
                throw std::runtime_error("Requested renderer choice is missing from the actual popup");
            click(ImVec2(x + width * .5f, y + height * .5f));
            expect(game.GetActiveSceneRenderPipeline() == before && game.IsSceneRenderPipelineSwitchPending(),
                "actual popup selection queues the switch without changing an in-flight renderer");
            expect(game.RequestSceneRenderPipeline(target, reason)
                && !game.RequestSceneRenderPipeline(before, reason) && !reason.empty(),
                "a queued switch merges duplicate requests and rejects a conflicting request");
            frame();
            expect(game.GetActiveSceneRenderPipeline() == target && !game.IsSceneRenderPipelineSwitchPending(),
                "requested renderer becomes active at the next frame boundary");
            if (game.GetActiveSceneRenderPipeline() != target)
                throw std::runtime_error(std::string("Unexpected active renderer: ") + game.GetActiveSceneRenderPipelineName());
            if (target == Pipeline::PathTracing)
                expect(g_rhi->RaytracingScene->RaytracingOutputPtr
                    && g_rhi->RaytracingScene->RaytracingOutputPtr != previousPathOutput,
                    "path tracing entry recreates accumulation storage, including re-entry");
            frames(3);
            bool sameBindings = objects == jObject::GetStaticRenderObject();
            for (size_t i = 0; sameBindings && i < objects.size(); ++i)
                sameBindings = objects[i]->GeometryDataPtr.get() == geometry[i] && objects[i]->MaterialPtr.get() == materials[i];
            expect(sameBindings, "switch preserves the live geometry and material instances without reimporting");
            expect(camera->Pos.IsNearlyEqual(cameraPosition) && camera->Target.IsNearlyEqual(cameraTarget)
                && camera->Up.IsNearlyEqual(cameraUp) && std::abs(camera->FOVRad - fov) < .00001f,
                "switch and toolbar clicks preserve the camera");
            expect(selection == editor.GetSelectedEntities() && dirty == editor.GetDocument().IsDirty()
                && document == editor.GetDocument().ToJson(),
                "switch preserves selection, authored scene and unsaved state");
            const auto capture = ReadScenePixels(jEditorUiRenderer::AcquireScenePreview(),
                output / (std::to_string(++step) + "-scene.png"), nullptr, Vector::ZeroVector);
            report["captures"].push_back({{"renderer", game.GetActiveSceneRenderPipelineName()}, {"pixels", capture}});
            expect(capture.value("readback", false) && capture.value("pngSaved", false)
                && capture.value("litPixels", size_t(0)) > 1000 && capture.value("distinctColorsAtMost1024", size_t(0)) > 32,
                "active renderer produces a nonblack textured scene through the real GPU");
        }
        // The normal swapchain capture hook records frame 60, including the
        // actual open selector. The separate captures above exclude editor UI.
        if (g_rhi->GetCurrentFrameNumber() < 56)
            frames(56 - g_rhi->GetCurrentFrameNumber());
        openCombo();
        frames(3);
        expect(popup() != nullptr, "renderer choices remain visible in the viewport capture");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true); frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); frame();
        const int gizmoFailures = RunViewportToolbarGizmoGpuTests(output, frame);
        expect(gizmoFailures == 0, "releasing a gizmo over the toolbar preserves Undo, Save and Play availability");
        if (pathSupported && !objects.empty() && objects.front()->GeometryDataPtr)
        {
            game.RequestSceneRenderPipeline(Pipeline::Deferred, reason); frame();
            g_rhi->Flush();
            auto& geometryData = *objects.front()->GeometryDataPtr;
            const auto indexBuffer = geometryData.IndexBufferPtr;
            geometryData.IndexBufferPtr.reset();
            const bool rejected = !game.CanSwitchSceneRenderPipeline(Pipeline::PathTracing, reason);
            geometryData.IndexBufferPtr = indexBuffer;
            expect(rejected && !reason.empty() && game.GetActiveSceneRenderPipeline() == Pipeline::Deferred,
                "unsupported non-indexed geometry is rejected before any GPU switch");
        }
        editor.ResetWorldBindings(false); game.ClearImportedSceneForAuthoring(); editor.GetDocument().NewScene();
        expect(!game.CanSwitchSceneRenderPipeline(Pipeline::PathTracing, reason) && !reason.empty()
            && !game.RequestSceneRenderPipeline(Pipeline::PathTracing, reason)
            && !game.IsSceneRenderPipelineSwitchPending(),
            "an empty scene disables path tracing and keeps a raster renderer");
    }
    catch (const std::exception& error)
    { ++failures; report["exception"] = error.what(); std::cerr << "FAIL renderer switch: " << error.what() << std::endl; }
    if (ImGui::GetCurrentContext()) ImGui::GetIO().AddMouseButtonEvent(0, false);
    SetAuthoringWorkspaceVisible(oldWorkspace); g_KeyState = oldKeys; g_MouseState = oldMouse;
    report["renderedFrames"] = rendered; report["failureCount"] = failures;
    report["status"] = failures ? "failed" : "passed";
    if (!output.empty()) Write(output / "renderer-switch-gpu-report.json", report.dump(2) + "\n");
    std::cout << "Renderer switch GPU workflow: " << rendered << " actual frames, " << failures << " failures" << std::endl;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
#else
    (void)outputDirectory;
    return EXIT_FAILURE;
#endif
}

std::string CaptureSceneFeaturePixels(const char* pngPath)
{
#ifdef ENABLE_EDITOR_FEATURES
    return ReadScenePixels(jEditorUiRenderer::AcquireScenePreview(), std::filesystem::path(pngPath),
        nullptr, Vector::ZeroVector).dump();
#else
    (void)pngPath;
    return "{\"available\":false,\"readback\":false}";
#endif
}

std::string CaptureSceneFeatureTexturePixels(const std::shared_ptr<jTexture>& texture, const char* pngPath)
{
#ifdef ENABLE_EDITOR_FEATURES
    return ReadScenePixels(texture, std::filesystem::path(pngPath), nullptr, Vector::ZeroVector).dump();
#else
    (void)texture; (void)pngPath;
    return "{\"available\":false,\"readback\":false}";
#endif
}

bool ReadSceneFeatureTextureFloats(const std::shared_ptr<jTexture>& texture,
    std::vector<float>& rgba, uint32_t& width, uint32_t& height, std::string& error)
{
#ifdef ENABLE_EDITOR_FEATURES
    if (!texture || !texture->Width || !texture->Height || texture->Width > 4096 || texture->Height > 4096
        || (texture->Format != ETextureFormat::RGBA32F && texture->Format != ETextureFormat::RGBA16F))
    { error = "Float readback requires a bounded RGBA32F/RGBA16F color texture"; return false; }
    const bool half = texture->Format == ETextureFormat::RGBA16F;
    const uint32 channels = 4, componentBytes = half ? 2 : 4;
    const uint32 sourceWidth = texture->Width, sourceHeight = texture->Height;
    uint32 rowPitch = sourceWidth * channels * componentBytes;
    uint64 size = uint64(rowPitch) * sourceHeight;
#ifdef _WIN32
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    if (IsUseDX12())
    {
        const auto description = static_cast<jTexture_DX12*>(texture.get())->Texture->Get()->GetDesc();
        uint32 rows = 0; uint64 rowBytes = 0;
        g_rhi_dx12->Device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, &rows, &rowBytes, &size);
        rowPitch = footprint.Footprint.RowPitch;
    }
#endif
    // Synchronous verification only. This wait is outside timed frame samples
    // and is never used by a production shadow pass.
    g_rhi->Flush();
    auto buffer = g_rhi->CreateRawBuffer(size, 512, EBufferCreateFlag::Readback,
        IsUseVulkan() ? EResourceLayout::UNDEFINED : EResourceLayout::TRANSFER_DST);
    if (!buffer || !buffer->GetHandle()) { error = "Float readback buffer allocation failed"; return false; }
    auto* command = g_rhi->BeginSingleTimeCommands();
    if (!command) { error = "Float readback command allocation failed"; return false; }
    const auto previous = texture->GetLayout();
    g_rhi->TransitionLayout(command, texture.get(), EResourceLayout::TRANSFER_SRC);
    g_rhi->TransitionLayout(command, buffer.get(), EResourceLayout::TRANSFER_DST);
    command->FlushBarrierBatch();
    if (IsUseVulkan())
    {
        const auto native = static_cast<jCommandBuffer_Vulkan*>(command)->GetRef();
        VkBufferImageCopy copy{};
        copy.bufferOffset = buffer->GetOffset();
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {sourceWidth, sourceHeight, 1};
        vkCmdCopyImageToBuffer(native, static_cast<jTexture_Vulkan*>(texture.get())->Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, static_cast<jBuffer_Vulkan*>(buffer.get())->Buffer, 1, &copy);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = static_cast<jBuffer_Vulkan*>(buffer.get())->Buffer;
        barrier.offset = buffer->GetOffset(); barrier.size = size;
        vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
#ifdef _WIN32
    else
    {
        D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
        source.pResource = static_cast<ID3D12Resource*>(texture->GetHandle());
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource = static_cast<ID3D12Resource*>(buffer->GetHandle());
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        destination.PlacedFootprint.Offset += buffer->GetOffset();
        static_cast<jCommandBuffer_DX12*>(command)->Get()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
#endif
    g_rhi->TransitionLayout(command, texture.get(), previous);
    command->FlushBarrierBatch();
    g_rhi->EndSingleTimeCommands(command);
    const auto* mapped = static_cast<const uint8*>(buffer->Map());
    if (!mapped) { error = "Float readback mapping failed"; return false; }
    std::vector<float> result(std::size_t(sourceWidth) * sourceHeight * channels);
    for (uint32 y = 0; y < sourceHeight; ++y)
        for (uint32 x = 0; x < sourceWidth; ++x)
            for (uint32 c = 0; c < channels; ++c)
            {
                const auto* sample = mapped + std::size_t(y) * rowPitch + (x * channels + c) * componentBytes;
                float value;
                if (half)
                {
                    uint16 bits; std::memcpy(&bits, sample, sizeof(bits));
                    const int exponent = (bits >> 10) & 31, mantissa = bits & 1023;
                    if (exponent == 31) value = mantissa ? std::numeric_limits<float>::quiet_NaN()
                        : std::numeric_limits<float>::infinity();
                    else value = exponent ? std::ldexp(float(1024 + mantissa), exponent - 25)
                        : std::ldexp(float(mantissa), -24);
                    if (bits & 0x8000) value = -value;
                }
                else std::memcpy(&value, sample, sizeof(value));
                result[(std::size_t(y) * sourceWidth + x) * channels + c] = value;
            }
    buffer->Unmap();
    rgba = std::move(result); width = sourceWidth; height = sourceHeight; error.clear();
    return true;
#else
    (void)texture; (void)rgba; (void)width; (void)height;
    error = "Editor GPU verification is unavailable"; return false;
#endif
}
