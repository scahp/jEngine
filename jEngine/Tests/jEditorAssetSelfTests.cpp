#include "pch.h"
#include "Code/Engine/jEditor.h"
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include "Code/Engine/ConsoleVariables/jConsoleVariable.h"
#include "FileLoader/jImageFileLoader.h"
#include "FileLoader/jModelLoader.h"
#include "FileLoader/jModelLoaderUtils.h"
#include "Scene/Light/jLight.h"
#include <fstream>

namespace
{
    void MakeTriangle(aiMesh& mesh)
    {
        mesh.mNumVertices = 3;
        mesh.mVertices = new aiVector3D[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        mesh.mNumFaces = 1;
        mesh.mFaces = new aiFace[1];
        mesh.mFaces[0].mNumIndices = 3;
        mesh.mFaces[0].mIndices = new unsigned int[3]{0, 1, 2};
    }

    void WriteDDS(const std::filesystem::path& path, uint32 format, uint32 width, uint32 height,
        uint32 mipCount, const std::vector<uint8>& pixels)
    {
        // DDS_HEADER followed by DDS_HEADER_DXT10. Keep the fixture independent
        // of either platform's decoder and compare the copied payload bytes.
        std::array<uint32, 37> header{};
        header[0] = 0x20534444; header[1] = 124; header[2] = 0x2100F;
        header[3] = height; header[4] = width; header[7] = mipCount;
        header[19] = 32; header[20] = 4; header[21] = 0x30315844;
        header[27] = mipCount > 1 ? 0x401008 : 0x1000;
        header[32] = format; header[33] = 3; header[35] = 1;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    }
}

int RunEditorAssetSelfTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
    };

    {
        aiMesh sparse, complete;
        MakeTriangle(sparse);
        MakeTriangle(complete);
        complete.mNormals = new aiVector3D[3]{{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
        complete.mTangents = new aiVector3D[3]{{0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
        complete.mBitangents = new aiVector3D[3]{{1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
        complete.mTextureCoords[0] = new aiVector3D[3]{{0.25f, 0.5f, 0}, {1, 0, 0}, {0, 1, 0}};
        jMeshData data;
        jSubMesh first, second;
        expect(jModelLoaderUtils::AppendMesh(sparse, data, first)
            && data.Vertices.size() == 3 && data.Normals.size() == 3
            && data.Tangents.size() == 3 && data.Bitangents.size() == 3 && data.TexCoord.size() == 3,
            "mesh without normals tangents or UVs has complete vertex streams");
        expect(jModelLoaderUtils::AppendMesh(complete, data, second)
            && second.StartVertex == 3 && data.Normals.size() == 6
            && data.Normals[0].y == 1 && data.Normals[3].z == 1
            && data.Tangents[3].y == 1 && data.TexCoord[3].x == 0.25f
            && data.Faces == std::vector<uint32>({0, 1, 2, 0, 1, 2}),
            "mixed optional mesh attributes retain matching submesh offsets");
        sparse.mFaces[0].mNumIndices = 2;
        expect(!jModelLoaderUtils::AppendMesh(sparse, data, first) && data.Vertices.size() == 6,
            "line faces are rejected before triangle index reads");
        sparse.mFaces[0].mNumIndices = 3;
        sparse.mFaces[0].mIndices[2] = 3;
        expect(!jModelLoaderUtils::AppendMesh(sparse, data, first) && data.Faces.size() == 6,
            "out of range mesh indices are rejected before writes");
        expect(jModelLoaderUtils::Requires32BitIndices({0, 65536, 1})
            && !jModelLoaderUtils::Requires32BitIndices({0, 65535, 1}),
            "index width follows largest local index instead of index count");
        expect(jModelLoaderUtils::ResolveTexturePath(".\\textures\\..\\diffuse.png", "root").generic_string() == "root/diffuse.png",
            "Windows texture separators resolve on both platforms");
    }

    {
        jThreadPool pool(2);
        std::atomic<int> completed{0};
        jTask task;
        task.pFunc = [&] { ++completed; };
        pool.Enqueue(task); pool.Enqueue(task); pool.Enqueue({});
        pool.WaitAll();
        expect(completed == 2, "unhashed tasks are distinct and empty tasks cannot stall completion");
        task.pHashFunc = [] { return size_t(42); };
        pool.Enqueue(task); pool.Enqueue(task);
        pool.WaitAll();
        expect(completed == 3, "asset task hashes deduplicate within a batch");
        pool.Enqueue(task); pool.WaitAll();
        expect(completed == 4, "completed asset hash may be loaded in a later batch");
        jTask failing;
        failing.pFunc = [] { throw std::runtime_error("expected asset failure"); };
        pool.Enqueue(failing); pool.Enqueue(task);
        bool caught = false;
        try { pool.WaitAll(); }
        catch (const std::runtime_error&) { caught = true; }
        expect(caught && completed == 5, "worker failures propagate after other tasks complete");
        pool.Enqueue(task); pool.WaitAll();
        expect(completed == 6, "asset pool remains usable after a failed batch");
        jThreadPool fallback(0);
        fallback.Enqueue(task); fallback.WaitAll();
        expect(completed == 7, "zero worker request still processes queued assets");
    }

#ifdef ENABLE_EDITOR_FEATURES
    {
        auto* oldContext = ImGui::GetCurrentContext();
        if (!oldContext)
            ImGui::CreateContext();
        const bool oldCapture = ImGui::GetIO().WantCaptureKeyboard;
        const auto oldKeys = g_KeyState;
        auto* oldEditor = g_Editor;
        jEditor editor;
        g_Editor = &editor;
        auto& placement = editor.Placement;
        placement.EnablePlacementMode = true;
        jCamera camera;
        jObject first, second;
        ResetAllKeyState();
        ImGui::GetIO().WantCaptureKeyboard = false;
        const auto releaseDelete = [&] {
            SetKeyDownState(EInputKey::DELETE_KEY, false);
            placement.ProcessInput(0, &camera, 1);
        };
        const auto pressDelete = [&] {
            releaseDelete();
            SetKeyDownState(EInputKey::DELETE_KEY, true);
            placement.ProcessInput(0, &camera, 1);
            releaseDelete();
        };
        jObject::AddObject(&first);
        jObject::AddObject(&second);
        placement.RegisterStaticObject(&first);
        expect(placement.PlacedObjects.size() == 2, "scene registration does not duplicate an editor entry");
        placement.SelectedRenderObjectIndices = {1, 2};
        placement.SelectObject(&second);
        expect(placement.SelectedRenderObjectIndices.empty(), "object selection clears previous mesh multi selection");
        placement.SelectObject(&first);
        ImGui::GetIO().WantCaptureKeyboard = true;
        pressDelete();
        expect(placement.PlacedObjects.size() == 2, "typing Delete while UI captures keyboard preserves scene objects");
        ImGui::GetIO().WantCaptureKeyboard = false;
        pressDelete();
        expect(placement.PlacedObjects.size() == 1 && placement.PlacedObjects[0].Object == &second
            && std::find(jObject::GetStaticObject().begin(), jObject::GetStaticObject().end(), &first) == jObject::GetStaticObject().end(),
            "deleting a registered object preserves its next neighbor after scene callback");
        placement.SelectObject(&second);
        pressDelete();
        expect(placement.PlacedObjects.empty(), "deleting last registered object does not erase past end");
        jObject debugObject;
        jLight light(ELightType::POINT);
        jObject::AddDebugObject(&debugObject);
        jLight::AddLights(&light);
        PlacedObjectInfo lightInfo;
        lightInfo.Type = EPlacedObjectType::LIGHT;
        lightInfo.Object = &debugObject;
        lightInfo.LightPtr = &light;
        placement.PlacedObjects.push_back(lightInfo);
        placement.SelectedPlacedObjectIndex = 0;
        pressDelete();
        expect(std::find(jObject::GetDebugObject().begin(), jObject::GetDebugObject().end(), &debugObject) == jObject::GetDebugObject().end()
            && std::find(jLight::GetLights().begin(), jLight::GetLights().end(), &light) == jLight::GetLights().end(),
            "light deletion removes its icon from the debug scene registry");
        placement.PickedObject = &first;
        placement.bPickRequested = true;
        placement.SelectedRenderObjectIndices = {0};
        placement.Clear();
        expect(!placement.PickedObject && !placement.bPickRequested && placement.SelectedRenderObjectIndices.empty(),
            "clearing editor state drops pending pick and mesh selections");
        g_Editor = oldEditor;
        g_KeyState = oldKeys;
        ImGui::GetIO().WantCaptureKeyboard = oldCapture;
        if (!oldContext)
            ImGui::DestroyContext();
    }
#endif

    {
        const auto directory = std::filesystem::temp_directory_path()
            / ("jengine-assets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        const auto oldApi = gAPIType;
        gAPIType = EAPIType::Vulkan; // CPU image packing does not require a device.
        auto& loader = jImageFileLoader::GetInstance();
        const auto path = directory / "pixel.DDS";
        const jName name(path.string());
        { std::ofstream file(path, std::ios::binary); file << "not a DDS"; }
        expect(loader.LoadImageDataFromFile(name).expired(), "malformed DDS returns failure without a worker exception");
        const std::vector<uint8> rgba{17, 34, 51, 255};
        WriteDDS(path, 28, 1, 1, 1, rgba);
        const auto decoded = loader.LoadImageDataFromFile(name).lock();
        expect(decoded && decoded->Width == 1 && decoded->Height == 1
            && decoded->Format == ETextureFormat::RGBA8 && decoded->ImageBulkData.ImageData == rgba,
            "uppercase DDS decodes and retries a previously failed path");
        expect(decoded && loader.LoadImageDataFromFile(name).lock() == decoded,
            "successful image decode is retained in the cache");
#ifdef _WIN32
        for (const auto format : {71u, 77u}) // BC1 and BC3, including 2x2/1x1 mips.
        {
            const size_t blockBytes = format == 71 ? 8 : 16;
            std::vector<uint8> blocks(blockBytes * 3);
            for (size_t i = 0; i < blocks.size(); ++i)
                blocks[i] = static_cast<uint8>(i + 1);
            const auto blockPath = directory / (std::to_string(format) + ".dds");
            WriteDDS(blockPath, format, 4, 4, 3, blocks);
            const auto image = loader.LoadImageDataFromFile(jName(blockPath.string())).lock();
            expect(image && image->MipLevel == 3 && image->ImageBulkData.ImageData == blocks
                && image->ImageBulkData.SubresourceFootprints[2].Offset == blockBytes * 2,
                format == 71 ? "BC1 Vulkan copy preserves complete blocks in every mip"
                             : "BC3 Vulkan copy preserves complete blocks in every mip");
        }
#else
        const auto unsupportedPath = directory / "unsupported.dds";
        WriteDDS(unsupportedPath, 71, 4, 4, 1, std::vector<uint8>(8, 1));
        expect(loader.LoadImageDataFromFile(jName(unsupportedPath.string())).expired(),
            "unsupported macOS DDS format returns failure without terminating asset workers");
#endif
        const jName missing((directory / "missing.dds").string());
        expect(loader.LoadTextureFromFile(missing).expired(), "missing texture returns failure before accessing RHI");
        gAPIType = oldApi;
        std::filesystem::remove_all(directory);
    }
    {
        auto& console = jConsole::Get();
        const std::array<const char*, 4> names{
            "audit.console.temporary.bool", "audit.console.temporary.int",
            "audit.console.temporary.float", "audit.console.temporary.string"};
        {
            jConsoleVariableBool boolean(names[0], false);
            jConsoleVariableInt integer(names[1], 7);
            jConsoleVariableFloat floating(names[2], 1.5f);
            jConsoleVariableString text(names[3], std::string("initial"));
            expect(console.FindVariable(names[0]) == &boolean && console.FindVariable(names[1]) == &integer
                && console.FindVariable(names[2]) == &floating && console.FindVariable(names[3]) == &text,
                "all console variable types register their live instance");
        }
        expect(std::all_of(names.begin(), names.end(), [&](const char* name) { return !console.FindVariable(name); }),
            "temporary console variable destruction removes every registry pointer");
        // These commands previously dereferenced the freed variables. They do
        // not need a GUI context and are checked by the sanitizer run too.
        console.ExecuteCommand("help audit.console.temporary.int");
        console.ExecuteCommand("list");

        const std::string duplicateName = "audit.console.duplicate";
        auto older = std::make_unique<jConsoleVariableInt>(duplicateName, 1);
        auto newer = std::make_unique<jConsoleVariableInt>(duplicateName, 2);
        older.reset();
        console.ExecuteCommand(duplicateName + " 21");
        expect(console.FindVariable(duplicateName) == newer.get() && newer->GetValue() == 21,
            "destroying an overwritten console variable preserves the active replacement");
        newer.reset();
        expect(!console.FindVariable(duplicateName), "destroying the active duplicate removes its registration");

        int integerValue = 7;
        float floatValue = 2.5f;
        bool boolValue = false;
        std::string stringValue;
        jConsoleVariableInt integer("audit.console.int", &integerValue);
        jConsoleVariableFloat floating("audit.console.float", &floatValue);
        jConsoleVariableBool boolean("audit.console.bool", &boolValue);
        jConsoleVariableString text("audit.console.string", &stringValue);
        integer.SetFromString(" +17 \t");
        bool validInteger = integerValue == 17;
        integer.SetFromString("-42");
        expect(validInteger && integerValue == -42, "signed whole integer values allow surrounding whitespace");
        integer.SetFromString(std::to_string(std::numeric_limits<int>::min()));
        validInteger = integerValue == std::numeric_limits<int>::min();
        integer.SetFromString(std::to_string(std::numeric_limits<int>::max()));
        expect(validInteger && integerValue == std::numeric_limits<int>::max(), "console integer parsing preserves both limits");
        bool rejectedIntegers = true;
        for (const char* value : {"12junk", "3.5", "0x10", "", " ", "2147483648", "-2147483649", "1 2", "--1", "nan", "inf"})
        {
            integerValue = 7;
            integer.SetFromString(value);
            rejectedIntegers &= integerValue == 7;
        }
        expect(rejectedIntegers, "invalid or overflowing integer text leaves the console value unchanged");
        console.ExecuteCommand("audit.console.int 12 extra");
        expect(integerValue == 7, "integer commands reject trailing argument tokens");
        console.ExecuteCommand("audit.console.int 19oops");
        expect(integerValue == 7, "integer commands reject a numeric prefix followed by garbage");

        floating.SetFromString(" -1.25e2 \t");
        bool validFloat = floatValue == -125.0f;
        floating.SetFromString("+3.5");
        expect(validFloat && floatValue == 3.5f, "finite decimal and exponent console values remain accepted");
        bool rejectedFloats = true;
        for (const char* value : {"nan", "NaN", "nan(payload)", "inf", "-inf", "infinity", "+Infinity",
            "3.5junk", "1.2.3", "", "\t", "1e1000", "1 2", "1e", "1e-1000"})
        {
            floatValue = 2.5f;
            floating.SetFromString(value);
            rejectedFloats &= floatValue == 2.5f;
        }
        expect(rejectedFloats, "nonfinite malformed and out of range float text cannot update console state");
        console.ExecuteCommand("audit.console.float 12.5 extra");
        expect(floatValue == 2.5f, "float commands reject trailing argument tokens");
        console.ExecuteCommand("audit.console.float nan");
        expect(floatValue == 2.5f, "float commands reject nonfinite values before updating external state");

        bool validBooleans = true;
        for (const char* value : {"TrUe", "1", "YES", " on "})
        {
            boolValue = false;
            boolean.SetFromString(value);
            validBooleans &= boolValue;
        }
        for (const char* value : {"FaLsE", "0", "NO", " off "})
        {
            boolValue = true;
            boolean.SetFromString(value);
            validBooleans &= !boolValue;
        }
        expect(validBooleans, "boolean tokens retain case insensitive aliases and surrounding whitespace");
        bool rejectedBooleans = true;
        for (const char* value : {"truejunk", "yes please", "2", "nan", "", "\x80"})
        {
            boolValue = false;
            boolean.SetFromString(value);
            rejectedBooleans &= !boolValue;
        }
        expect(rejectedBooleans, "boolean parsing rejects partial tokens and invalid byte input");
        console.ExecuteCommand("audit.console.bool true false");
        expect(!boolValue, "boolean commands reject trailing argument tokens");
        console.ExecuteCommand("audit.console.string hello asset editor");
        expect(stringValue == "hello asset editor", "strict scalar parsing preserves multiword string commands");
    }
    return failures;
}
