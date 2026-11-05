#include "pch.h"
#include "jEditorProcedural.h"
#include "FileLoader/jEditorAssetPath.h"
#include "Code/Engine/jEditor.h"
#include "FileLoader/jImageFileLoader.h"
#include "Material/jMaterial.h"
#include "Material/jMaterialWater.h"
#include "jEditorWater.h"
#include "Scene/jObject.h"
#include "Scene/jMeshObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "RHI/jRHI.h"
#include "RHI/jRaytracingScene.h"
#include "RHI/Vulkan/jRHI_Vulkan.h"
#include "RHI/Vulkan/jBuffer_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jRHI_DX12.h"
#include "RHI/DX12/jBuffer_DX12.h"
#include "RHI/DX12/jCommandBufferManager_DX12.h"
#endif
#include <cmath>

namespace jEditorProcedural
{
using namespace jEditorAuthoring;
namespace
{
Vector ToVector(Vec3 v) { return Vector(v.X, v.Y, v.Z); }
Vec3 ToVec3(Vector v) { return {v.x, v.y, v.z}; }
float Random01(uint32_t& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return float(state >> 8) / 16777216.0f;
}
}
bool BuildTerrainMesh(const TerrainProperties& terrain, Mesh& mesh, std::string& error)
{
    const uint32_t n = terrain.Resolution;
    if (n < 2 || n > 257 || !std::isfinite(terrain.SizeX) || !std::isfinite(terrain.SizeZ) ||
        terrain.SizeX <= 0 || terrain.SizeZ <= 0 ||
        (!terrain.Heights.empty() && terrain.Heights.size() != size_t(n) * n))
    { error = "Terrain requires a 2..257 grid, positive finite size and one height per grid vertex."; return false; }
    for (float height : terrain.Heights)
        if (!std::isfinite(height)) { error = "Terrain height must be finite."; return false; }
    Mesh next;
    next.Vertices.resize(size_t(n) * n);
    next.Indices.reserve(size_t(n - 1) * (n - 1) * 6);
    const auto height = [&](uint32_t x, uint32_t z)
        { return terrain.Heights.empty() ? 0.0f : terrain.Heights[size_t(z) * n + x]; };
    for (uint32_t z = 0; z < n; ++z) for (uint32_t x = 0; x < n; ++x)
    {
        auto& v = next.Vertices[size_t(z) * n + x];
        v.U = float(x) / float(n - 1); v.V = float(z) / float(n - 1);
        v.Position = {(v.U - 0.5f) * terrain.SizeX, height(x, z), (v.V - 0.5f) * terrain.SizeZ};
        const uint32_t left = x ? x - 1 : x, right = std::min(x + 1, n - 1);
        const uint32_t nearZ = z ? z - 1 : z, farZ = std::min(z + 1, n - 1);
        const double slopeX = (double(height(right, z)) - height(left, z)) / ((right - left) * double(terrain.SizeX) / (n - 1));
        const double slopeZ = (double(height(x, farZ)) - height(x, nearZ)) / ((farZ - nearZ) * double(terrain.SizeZ) / (n - 1));
        const double length = std::hypot(slopeX, slopeZ, 1.0);
        v.Normal = {float(-slopeX / length), float(1 / length), float(-slopeZ / length)};
        if (x + 1 < n && z + 1 < n)
        {
            const uint32_t i = z * n + x;
            // Counter-clockwise from above, shared vertices preserve smooth normals.
            next.Indices.insert(next.Indices.end(), {i, i + n, i + 1, i + 1, i + n, i + n + 1});
        }
    }
    mesh = std::move(next); error.clear(); return true;
}
void ParticleSimulation::Reset(const ParticleProperties& settings)
{
    Live.clear(); Live.reserve(std::min(settings.MaxParticles, 16384u));
    RandomState = settings.Seed ? settings.Seed : 0x9e3779b9u; SpawnRemainder = 0;
}
void ParticleSimulation::Advance(const ParticleProperties& settings, float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0) return;
    const float dt = std::min(deltaTime, 0.25f);
    const uint32_t capacity = std::min(settings.MaxParticles, 16384u);
    const float lifetime = std::max(0.001f, settings.Lifetime);
    if (!std::isfinite(lifetime) || !std::isfinite(settings.SpawnRate) || !std::isfinite(settings.Speed) ||
        !std::isfinite(settings.Gravity.X) || !std::isfinite(settings.Gravity.Y) || !std::isfinite(settings.Gravity.Z)) return;
    for (auto& particle : Live)
    {
        particle.Age += dt;
        particle.Position.X += particle.Velocity.X * dt + settings.Gravity.X * (0.5f * dt * dt);
        particle.Position.Y += particle.Velocity.Y * dt + settings.Gravity.Y * (0.5f * dt * dt);
        particle.Position.Z += particle.Velocity.Z * dt + settings.Gravity.Z * (0.5f * dt * dt);
        particle.Velocity.X += settings.Gravity.X * dt;
        particle.Velocity.Y += settings.Gravity.Y * dt;
        particle.Velocity.Z += settings.Gravity.Z * dt;
    }
    std::erase_if(Live, [&](const Particle& p) { return p.Age >= lifetime ||
        !std::isfinite(p.Position.X) || !std::isfinite(p.Position.Y) || !std::isfinite(p.Position.Z); });
    if (Live.size() > capacity) Live.resize(capacity);
    if (!settings.Enabled || !capacity) return;
    SpawnRemainder += std::clamp(double(settings.SpawnRate), 0.0, 100000.0) * dt;
    const size_t spawn = std::min(size_t(SpawnRemainder), size_t(capacity) - Live.size());
    SpawnRemainder -= std::floor(SpawnRemainder); // No accumulated backlog at capacity.
    for (size_t i = 0; i < spawn; ++i)
    {
        Particle p;
        const float angle = Random01(RandomState) * 6.283185307f;
        const float radial = Random01(RandomState) * 0.6f;
        p.Velocity = {std::cos(angle) * radial * settings.Speed,
            (0.65f + Random01(RandomState) * 0.35f) * settings.Speed,
            std::sin(angle) * radial * settings.Speed};
        Live.push_back(p);
    }
}

#ifdef ENABLE_EDITOR_FEATURES
namespace
{
std::shared_ptr<jRenderObjectGeometryData> UploadMesh(const Mesh& mesh)
{
    if (mesh.Vertices.empty() || !g_rhi) return {};
    auto stream = std::make_shared<jVertexStreamData>();
    auto positions = std::make_shared<jVertexStreamData>();
    auto data = std::make_shared<jStreamParam<jBaseVertex>>();
    auto posData = std::make_shared<jStreamParam<jPositionOnlyVertex>>();
    data->Stride = sizeof(jBaseVertex); data->Name = jNameStatic("jBaseVertex");
    data->Attributes = {
        {jNameStatic("POSITION"), EBufferElementType::FLOAT, 12},
        {jNameStatic("NORMAL"), EBufferElementType::FLOAT, 12},
        {jNameStatic("TANGENT"), EBufferElementType::FLOAT, 12},
        {jNameStatic("BITANGENT"), EBufferElementType::FLOAT, 12},
        {jNameStatic("TEXCOORD"), EBufferElementType::FLOAT, 8}};
    posData->Stride = sizeof(jPositionOnlyVertex); posData->Name = jNameStatic("jPositionOnlyVertex");
    posData->Attributes = {{jNameStatic("POSITION"), EBufferElementType::FLOAT, 12}};
    for (const auto& v : mesh.Vertices)
    {
        jBaseVertex vertex;
        vertex.Pos = ToVector(v.Position); vertex.Normal = ToVector(v.Normal);
        vertex.Tangent = Vector(1, 0, 0);
        vertex.Bitangent = vertex.Normal.CrossProduct(vertex.Tangent).GetNormalize();
        vertex.TexCoord = Vector2(v.U, v.V);
        data->Data.push_back(vertex); posData->Data.push_back({vertex.Pos});
    }
    stream->Params.push_back(data); positions->Params.push_back(posData);
    stream->ElementCount = positions->ElementCount = int32(mesh.Vertices.size());
    std::shared_ptr<jIndexStreamData> indices;
    if (!mesh.Indices.empty())
    {
        indices = std::make_shared<jIndexStreamData>();
        auto* indexData = new jStreamParam<uint32>();
        indexData->Stride = 12; indexData->Name = jNameStatic("Index");
        indexData->Attributes = {{jNameStatic("INDEX"), EBufferElementType::UINT32, 12}};
        indexData->Data = mesh.Indices;
#if LEFT_HANDED
        // CPU terrain/Recast triangles use mathematical +Y winding. The engine's
        // LH raster front-face convention matches CreateQuad's opposite winding;
        // convert only the uploaded indices, retaining CPU/navigation topology.
        for (size_t triangle = 0; triangle + 2 < indexData->Data.size(); triangle += 3)
            std::swap(indexData->Data[triangle + 1], indexData->Data[triangle + 2]);
#endif
        indices->Param = indexData;
        indices->ElementCount = uint32(mesh.Indices.size());
    }
    return std::make_shared<jRenderObjectGeometryData>(stream, positions, indices);
}

// The frame slot is acquired after its GPU fence. Only that slot's host-visible
// vertex pages are overwritten; earlier frame data and topology stay untouched.
class ParticleRenderObject final : public jRenderObject
{
public:
    std::vector<jBaseVertex> Vertices;
    mutable std::vector<std::shared_ptr<jBuffer>> FrameVertices, FramePositions;
    mutable uint32 PreparedFrame = UINT32_MAX, PreparedSlot = UINT32_MAX;
    bool IsSupportRaytracing() const override { return false; }
    bool PrepareBuffers(const std::shared_ptr<jRenderFrameContext>& frame, bool positionOnly) const
    {
        if (Vertices.empty()) return false;
        const size_t slot = frame->FrameIndex;
        if (slot > 16) return false;
        if (FrameVertices.size() <= slot) { FrameVertices.resize(slot + 1); FramePositions.resize(slot + 1); }
        auto& buffer = positionOnly ? FramePositions[slot] : FrameVertices[slot];
        const size_t stride = positionOnly ? sizeof(jPositionOnlyVertex) : sizeof(jBaseVertex);
        const size_t bytes = Vertices.size() * stride;
        if (!buffer || buffer->GetBufferSize() < bytes)
            buffer = g_rhi->CreateRawBuffer(bytes, 0, EBufferCreateFlag::CPUAccess | EBufferCreateFlag::VertexBuffer,
                // DX12 upload heaps must remain GENERIC_READ (READ_ONLY).
                IsUseVulkan() ? EResourceLayout::GENERAL : EResourceLayout::READ_ONLY);
        if (!buffer) return false;
        void* pointer = buffer->GetMappedPointer();
        if (g_rhi == g_rhi_vk)
        {
            auto* native = static_cast<jBuffer_Vulkan*>(buffer.get());
            pointer = native->HasBufferOwnership && pointer ? pointer : native->Map();
        }
        else if (!pointer) pointer = buffer->Map();
        auto* mapped = static_cast<uint8*>(pointer);
        if (!mapped) return false;
        if (positionOnly)
            for (size_t i = 0; i < Vertices.size(); ++i)
                memcpy(mapped + i * stride, &Vertices[i].Pos, stride);
        else memcpy(mapped, Vertices.data(), bytes);
        return true;
    }
    void Prepare(const std::shared_ptr<jRenderFrameContext>& frame) const
    {
        PreparedFrame = PreparedSlot = UINT32_MAX;
        if (frame && PrepareBuffers(frame, false) && PrepareBuffers(frame, true))
        { PreparedFrame = g_rhi->GetCurrentFrameNumber(); PreparedSlot = frame->FrameIndex; }
    }
    bool IsPrepared(const std::shared_ptr<jRenderFrameContext>& frame) const
    {
        return frame && PreparedSlot == frame->FrameIndex && PreparedFrame == g_rhi->GetCurrentFrameNumber();
    }
    void BindBuffers(const std::shared_ptr<jRenderFrameContext>& frame, bool positionOnly,
        const jVertexBuffer* overrideInstance = nullptr) const override
    {
        (void)overrideInstance;
        if (Vertices.empty() || !IsPrepared(frame)) return;
        const size_t slot = frame->FrameIndex;
        const auto& pages = positionOnly ? FramePositions : FrameVertices;
        if (slot >= pages.size() || !pages[slot]) return;
        const auto& buffer = pages[slot];
        const size_t stride = positionOnly ? sizeof(jPositionOnlyVertex) : sizeof(jBaseVertex);
        const size_t bytes = Vertices.size() * stride;
        if (g_rhi == g_rhi_vk)
        {
            auto* vk = static_cast<jBuffer_Vulkan*>(buffer.get());
            const VkDeviceSize offset = vk->Offset;
            vkCmdBindVertexBuffers((VkCommandBuffer)frame->GetActiveCommandBuffer()->GetHandle(), 0, 1, &vk->Buffer, &offset);
        }
#ifdef _WIN32
        else if (g_rhi == g_rhi_dx12)
        {
            auto* dx = static_cast<jBuffer_DX12*>(buffer.get());
            D3D12_VERTEX_BUFFER_VIEW view{dx->GetGPUAddress(), UINT(bytes), UINT(stride)};
            static_cast<jCommandBuffer_DX12*>(frame->GetActiveCommandBuffer())->CommandList->IASetVertexBuffers(0, 1, &view);
        }
#endif
    }
    void Draw(const std::shared_ptr<jRenderFrameContext>& frame, int32 instanceCount = 1) override
    { if (!Vertices.empty() && IsPrepared(frame)) g_rhi->DrawArrays(frame, EPrimitiveType::TRIANGLES, 0, int32(Vertices.size())); }
    void Draw(const std::shared_ptr<jRenderFrameContext>& frame, int32, int32, int32, int32, int32) override { Draw(frame); }
};
class ProceduralObject final : public jObject
{
public:
    TerrainProperties Terrain;
    ParticleProperties Settings;
    ParticleSimulation Simulation;
    bool ParticleEmitter = false;
    float LastPlayTime = 0;
    std::optional<WaterProperties> Water;
    std::shared_ptr<jMaterialWaterState> WaterState;
};
struct MaterialState
{
    MaterialProperties Last{};
    bool HasLast = false;
    std::filesystem::path ProjectRoot;
    std::vector<std::shared_ptr<jMaterial>> Originals;
};
struct Adapter
{
    jEditor* Editor = nullptr;
    std::map<jObjectID, MaterialState> Materials;
    void ApplyMaterial(const MaterialProperties& properties, jObject& object)
    {
        if (Materials.size() > 1024)
            std::erase_if(Materials, [](const auto& entry) { return !jObject::FindObjectByID(entry.first); });
        auto& state = Materials[object.ObjectID];
        // Runtime decides when the material is dirty. An identical payload may
        // still be a texture reimport, so do not suppress its cache epoch lookup.
        state.ProjectRoot = Editor->GetProjectRoot();
        if (state.Originals.size() != object.RenderObjects.size())
        {
            state.Originals.clear();
            for (auto* render : object.RenderObjects) state.Originals.push_back(render->MaterialPtr ? render->MaterialPtr : GDefaultMaterial);
        }
        if (!properties.OverrideEnabled)
        {
            for (size_t i = 0; i < object.RenderObjects.size(); ++i)
            {
                auto* render = object.RenderObjects[i];
                if (render->MaterialPtr && state.Originals[i] &&
                    render->MaterialPtr->IsRaytracingAlphaTestEnabled() != state.Originals[i]->IsRaytracingAlphaTestEnabled() &&
                    g_rhi && g_rhi->RaytracingScene && render->IsSupportRaytracing())
                    g_rhi->RaytracingScene->MarkBLASDirty(render);
                object.RenderObjects[i]->MaterialPtr = state.Originals[i];
            }
            state.Last = properties; state.HasLast = true;
            return;
        }
        auto load = [&](const std::string& relative) -> std::shared_ptr<jTexture>
        {
            if (relative.empty() || relative.find('\\') != std::string::npos) return {};
            const auto path = jEditorAssetPath::FromUtf8(relative);
            if (path.is_absolute()) return {};
            for (const auto& component : path) if (component == "..") return {};
            std::error_code ec;
            const auto root = std::filesystem::weakly_canonical(Editor->GetProjectRoot(), ec);
            if (ec) return {};
            const auto full = std::filesystem::weakly_canonical(root / path, ec);
            if (ec) return {};
            auto rootIt = root.begin(), fullIt = full.begin();
            for (; rootIt != root.end(); ++rootIt, ++fullIt)
                if (fullIt == full.end() || *rootIt != *fullIt) return {};
            return jImageFileLoader::GetInstance().LoadTextureOwned(jName(jEditorAssetPath::ToUtf8(full).c_str()));
        };
        const auto albedo = load(properties.BaseColorTexture), normal = load(properties.NormalTexture);
#ifdef _WIN32
        if ((albedo || normal) && g_rhi == g_rhi_dx12) g_rhi->Flush();
#endif
        for (size_t i = 0; i < object.RenderObjects.size(); ++i)
        {
            auto* render = object.RenderObjects[i];
            const auto& source = state.Originals[i];
            std::shared_ptr<jMaterial> material;
            if (auto meshMaterial = std::dynamic_pointer_cast<jMeshMaterial>(source))
                material = std::make_shared<jMeshMaterial>(*meshMaterial);
            else material = source ? std::make_shared<jMaterial>(*source) : std::make_shared<jMaterial>();
            // A clone must not Free the original instance's descriptor set.
            material->ShaderBindingInstance.reset(); material->NeedToUpdateShaderBindingInstance = true;
            material->UseMaterialProperties = true;
            material->BaseColorOpacity = Vector4(ToVector(properties.BaseColor), properties.Opacity);
            material->Emissive = ToVector(properties.Emissive);
            material->MetallicFactor = properties.Metallic; material->RoughnessFactor = properties.Roughness;
            material->Revision = render->MaterialPtr ? render->MaterialPtr->Revision + 1 : 1;
            if (albedo)
            {
                material->TexData[int(jMaterial::EMaterialTextureType::Albedo)].Texture = albedo.get();
                material->OwnedTextures.push_back(albedo);
            }
            if (normal)
            {
                material->TexData[int(jMaterial::EMaterialTextureType::Normal)].Texture = normal.get();
                material->OwnedTextures.push_back(normal);
            }
            // Authored untextured meshes still need the normal map/alpha shader
            // permutation; white is a real neutral texture, not an asset stand-in.
            if (!material->TexData[int(jMaterial::EMaterialTextureType::Albedo)].Texture)
            {
                material->TexData[int(jMaterial::EMaterialTextureType::Albedo)].Texture = GWhiteTexture.get();
                material->OwnedTextures.push_back(GWhiteTexture);
            }
            material->bRaytracingAlphaTest = true;
            if (render->MaterialPtr && !render->MaterialPtr->IsRaytracingAlphaTestEnabled() &&
                g_rhi->RaytracingScene && render->IsSupportRaytracing())
                g_rhi->RaytracingScene->MarkBLASDirty(render);
            render->MaterialPtr = std::move(material);
        }
        state.Last = properties; state.HasLast = true;
    }
};
}
void PrepareRenderResources(const std::shared_ptr<jRenderFrameContext>& frame)
{
    if (!frame || !g_rhi) return;
    // The acquired slot's fence has completed. Prepare host-visible pages before
    // VkRenderPass begins, so allocation transitions never reach an in-pass draw.
    for (auto* render : jObject::GetStaticRenderObject())
        if (auto* particles = dynamic_cast<ParticleRenderObject*>(render))
            particles->Prepare(frame);
}
void Install(jEditor& editor)
{
    auto adapter = std::make_shared<Adapter>(); adapter->Editor = &editor;
    editor.SetMaterialApplier([adapter](const MaterialProperties& material, jObject& object)
        { adapter->ApplyMaterial(material, object); });
    editor.SetProceduralFactory([](const Entity& entity, std::string& error) -> std::shared_ptr<jObject>
    {
        if (!g_rhi) { error = "Procedural publication requires the render owner."; return {}; }
        auto object = std::make_shared<ProceduralObject>();
        Mesh mesh;
        jRenderObject* render = nullptr;
        if (entity.Kind == EntityKind::Water)
        {
            if (!jEditorWater::Validate(entity.Water, error)) return {};
            TerrainProperties grid;
            grid.Resolution = entity.Water.Resolution;
            grid.SizeX = entity.Water.SizeX; grid.SizeZ = entity.Water.SizeZ;
            if (!BuildTerrainMesh(grid, mesh, error)) return {};
            object->Water = entity.Water;
            object->WaterState = std::make_shared<jMaterialWaterState>();
            object->WaterState->Settings = entity.Water.Waves;
            render = new jRenderObject();
        }
        else if (entity.Kind == EntityKind::Terrain)
        {
            if (!BuildTerrainMesh(entity.Terrain, mesh, error)) return {};
            object->Terrain = entity.Terrain; render = new jRenderObject();
        }
        else if (entity.Kind == EntityKind::ParticleEmitter)
        {
            object->ParticleEmitter = true; object->Settings = entity.Particles;
            object->Simulation.Reset(entity.Particles);
            render = new ParticleRenderObject();
            mesh.Vertices.resize(6); // Layout prototype; actual draws use live particles.
            object->SetSkipShadowMapGen(true);
        }
        else { error = "Unsupported procedural entity kind."; return {}; }
        auto geometry = UploadMesh(mesh);
        if (!geometry || !geometry->VertexBufferPtr || !geometry->VertexBuffer_PositionOnlyPtr ||
            !geometry->VertexBufferPtr->GetBuffer(0) || !geometry->VertexBuffer_PositionOnlyPtr->GetBuffer(0))
        { delete render; error = "Procedural geometry upload failed."; return {}; }
        // Geometry convenience APIs may submit copy-queue work. Publish only
        // after completion, including the first BLAS consumer on another queue.
        g_rhi->Flush();
        render->CreateRenderObject(geometry);
        render->MaterialPtr = GDefaultMaterial; render->IsTwoSided = object->ParticleEmitter;
        if (object->WaterState)
        {
            render->MaterialPtr = std::make_shared<jMaterial>(*GDefaultMaterial);
            render->MaterialPtr->ShaderBindingInstance.reset();
            render->MaterialPtr->NeedToUpdateShaderBindingInstance = true;
            render->MaterialPtr->Water = object->WaterState;
            jWaterSimulation::Bounds bounds;
            if (jWaterSimulation::ConservativeBounds(entity.Water.Waves,
                {-entity.Water.SizeX * .5f, -entity.Water.SizeZ * .5f},
                {entity.Water.SizeX * .5f, entity.Water.SizeZ * .5f}, bounds, error))
                object->BoundBox = {Vector(bounds.Min[0], bounds.Min[1], bounds.Min[2]),
                    Vector(bounds.Max[0], bounds.Max[1], bounds.Max[2])};
        }
        object->AddRenderObject(render); object->RenderObjectGeometryDataPtr = render->GeometryDataPtr;
        error.clear(); return object;
    }, [](const Entity& entity, jObject& base, float deltaTime, float playTime)
    {
        auto* object = dynamic_cast<ProceduralObject*>(&base);
        if (!object || object->RenderObjects.empty()) return;
        auto* render = object->RenderObjects[0];
        if (object->Water && object->WaterState)
        {
            if (entity.Kind != EntityKind::Water) return;
            if (*object->Water != entity.Water)
            {
                std::string error;
                if (!jEditorWater::Validate(entity.Water, error)) return;
                const bool topologyChanged = object->Water->Resolution != entity.Water.Resolution
                    || object->Water->SizeX != entity.Water.SizeX || object->Water->SizeZ != entity.Water.SizeZ;
                if (topologyChanged)
                {
                    TerrainProperties grid; grid.Resolution = entity.Water.Resolution;
                    grid.SizeX = entity.Water.SizeX; grid.SizeZ = entity.Water.SizeZ;
                    Mesh mesh;
                    if (!BuildTerrainMesh(grid, mesh, error)) return;
                    g_rhi->Flush();
                    auto geometry = UploadMesh(mesh);
                    if (!geometry || !geometry->VertexBufferPtr || !geometry->VertexBuffer_PositionOnlyPtr) return;
                    g_rhi->Flush();
                    render->GeometryDataPtr = geometry; object->RenderObjectGeometryDataPtr = geometry;
                    render->BottomLevelASBuffer.reset(); render->ScratchASBuffer.reset();
                    if (g_rhi->RaytracingScene) g_rhi->RaytracingScene->MarkBLASDirty(render);
                }
                object->WaterState->Settings = entity.Water.Waves;
                *object->Water = entity.Water;
                jWaterSimulation::Bounds bounds;
                if (jWaterSimulation::ConservativeBounds(entity.Water.Waves,
                    {-entity.Water.SizeX * .5f, -entity.Water.SizeZ * .5f},
                    {entity.Water.SizeX * .5f, entity.Water.SizeZ * .5f}, bounds, error))
                    object->BoundBox = {Vector(bounds.Min[0], bounds.Min[1], bounds.Min[2]),
                        Vector(bounds.Max[0], bounds.Max[1], bounds.Max[2])};
                if (render->MaterialPtr) render->MaterialPtr->NeedToUpdateShaderBindingInstance = true;
            }
            object->WaterState->TimeSeconds = playTime;
            if (render->MaterialPtr && render->MaterialPtr->Water != object->WaterState)
            {
                render->MaterialPtr->Water = object->WaterState;
                render->MaterialPtr->NeedToUpdateShaderBindingInstance = true;
            }
            return;
        }
        if (!object->ParticleEmitter)
        {
            if (object->Terrain == entity.Terrain) return;
            Mesh mesh; std::string error;
            if (!BuildTerrainMesh(entity.Terrain, mesh, error)) return;
            // Editing topology is uncommon; use the established completion boundary
            // before replacing buffers/AS rather than freeing an in-flight generation.
            g_rhi->Flush();
            auto geometry = UploadMesh(mesh);
            if (!geometry || !geometry->VertexBufferPtr || !geometry->VertexBuffer_PositionOnlyPtr) return;
            g_rhi->Flush();
            render->GeometryDataPtr = geometry; object->RenderObjectGeometryDataPtr = geometry;
            object->Terrain = entity.Terrain;
            render->BottomLevelASBuffer.reset(); render->ScratchASBuffer.reset();
            if (g_rhi->RaytracingScene) g_rhi->RaytracingScene->MarkBLASDirty(render);
            return;
        }
        auto* particles = static_cast<ParticleRenderObject*>(render);
        if (object->Settings != entity.Particles)
        { object->Simulation.Reset(entity.Particles); object->Settings = entity.Particles; }
        if (playTime < object->LastPlayTime) object->Simulation.Reset(entity.Particles);
        object->LastPlayTime = playTime;
        if (playTime > 0) object->Simulation.Advance(entity.Particles, deltaTime);
        else if (object->Simulation.Particles().empty()) object->Simulation.Advance(entity.Particles, 1.0f / 30.0f);
        Vector right(1, 0, 0), up(0, 1, 0);
        if (auto* camera = jCamera::GetMainCamera())
        {
            const Matrix inverse = render->World.GetInverse();
            right = inverse.TransformDirection(camera->GetRightVector()).GetNormalize();
            up = inverse.TransformDirection(camera->GetUpVector()).GetNormalize();
        }
        const float half = std::max(0.001f, entity.Particles.Size) * 0.5f;
        const Vector offsets[] = {(-right-up)*half, (right-up)*half, (right+up)*half,
            (-right-up)*half, (right+up)*half, (-right+up)*half};
        const Vector2 uvs[] = {{0,1},{1,1},{1,0},{0,1},{1,0},{0,0}};
        particles->Vertices.clear(); particles->Vertices.reserve(object->Simulation.Particles().size() * 6);
        for (const auto& p : object->Simulation.Particles()) for (size_t i = 0; i < 6; ++i)
        {
            jBaseVertex v; v.Pos = ToVector(p.Position) + offsets[i];
            v.Normal = right.CrossProduct(up).GetNormalize(); v.Tangent = right; v.Bitangent = up; v.TexCoord = uvs[i];
            particles->Vertices.push_back(v);
        }
        if (render->MaterialPtr && render->MaterialPtr->UseMaterialProperties)
        {
            const Vector color = ToVector(entity.Particles.Color);
            const Vector4 baseColor(ToVector(entity.Material.BaseColor) * color, entity.Material.Opacity);
            const Vector emissive = ToVector(entity.Material.Emissive) + color;
            if (render->MaterialPtr->BaseColorOpacity != baseColor || render->MaterialPtr->Emissive != emissive)
            {
                render->MaterialPtr->BaseColorOpacity = baseColor;
                render->MaterialPtr->Emissive = emissive;
                ++render->MaterialPtr->Revision;
            }
        }
    });
}
#else
void Install(jEditor&) {}
#endif
}
