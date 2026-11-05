#include "pch.h"
#include "jAdvancedShadowPass.h"
#include "Features/jAdvancedShadowMath.h"
#include "Features/jShadowFiltering.h"
#include "jDrawCommand.h"
#include "jSceneRenderTargets.h"
#include "Material/jMaterial.h"
#include "Core/jLog.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jResourceBarrierBatcher.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/Vulkan/jVertexBuffer_Vulkan.h"
#include "RHI/Vulkan/jIndexBuffer_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jVertexBuffer_DX12.h"
#include "RHI/DX12/jIndexBuffer_DX12.h"
#endif
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jMeshObject.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Shader/jLightingShaderParameters.h"
#include "jOptions.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <limits>

namespace jAdvancedShadows
{
namespace
{
namespace Math = jAdvancedShadowMath;
constexpr std::size_t MiB = 1024 * 1024;
constexpr std::size_t PerLightTargetBudget = 192 * MiB;
constexpr std::size_t PerViewTargetBudget = 384 * MiB;
constexpr std::size_t GeometryCacheBudget = 64 * MiB;
constexpr std::size_t MaximumFrameInputTriangles = 131072;
constexpr std::size_t MaximumFrameOutputTriangles = 524288;
constexpr std::size_t MaximumAdvancedLights = 16;
FrameStats LastStats;
std::shared_ptr<IUniformBufferBlock> NeutralUniform;
std::mutex NeutralMutex;
std::map<const jLight*, std::string> LastDiagnostic;
struct DebugOutput
{
    jAdvancedShadowData Values;
    std::weak_ptr<jRenderTarget> Map, Deep0, Deep1;
};
std::map<const jLight*, DebugOutput> DebugOutputs;

const jSamplerStateInfo* AtlasSampler()
{
    // FP32 atlas filtering is explicit in HLSL; hardware linear support for
    // RGBA32F is optional on Vulkan/Metal devices.
    return TSamplerStateInfo<ETextureFilter::NEAREST, ETextureFilter::NEAREST,
        ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE,
        ETextureAddressMode::CLAMP_TO_EDGE>::Create();
}

std::shared_ptr<IUniformBufferBlock> Neutral()
{
    std::lock_guard<std::mutex> lock(NeutralMutex);
    if (!NeutralUniform)
    {
        jAdvancedShadowData value{};
        for (auto& matrix : value.ShadowVP) matrix.SetIdentity();
        NeutralUniform = g_rhi->CreateUniformBufferBlock(jNameStatic("NeutralAdvancedShadow"),
            jLifeTimeType::MultiFrame, sizeof(value));
        if (NeutralUniform) NeutralUniform->UpdateBufferData(&value, sizeof(value));
    }
    return NeutralUniform;
}

void Flush(const std::shared_ptr<jRenderFrameContext>& frame)
{
#if USE_RESOURCE_BARRIER_BATCHER
    const auto* command = frame->GetActiveCommandBuffer();
    if (auto* global = g_rhi->GetGlobalBarrierBatcher()) global->Flush(command);
    command->FlushBarrierBatch();
#endif
}

Math::Vec3 ToVec(const Vector& value) { return {value.x, value.y, value.z}; }
Math::Mat4 ToMath(const Matrix& value)
{
    Math::Mat4 result;
    for (std::size_t i = 0; i < 16; ++i) result[i] = value.mm[i];
    return result;
}
Matrix ToMatrix(const Math::Mat4& value)
{
    Matrix result;
    for (std::size_t i = 0; i < 16; ++i) result.mm[i] = static_cast<float>(value[i]);
    return result;
}

bool IsRequested(const Settings& settings, ELightType type)
{
    return settings.Method != Representation::Depth
        || (type == ELightType::DIRECTIONAL && settings.DirectionalProjection != Projection::Single)
        || (type == ELightType::POINT && settings.PointProjection != PointProjection::Cube);
}

LightKind Kind(ELightType type)
{
    return type == ELightType::DIRECTIONAL ? LightKind::Directional
        : type == ELightType::POINT ? LightKind::Point : LightKind::Spot;
}

struct Output
{
    jAdvancedShadowData Values{};
    std::shared_ptr<IUniformBufferBlock> Uniform;
    std::shared_ptr<jRenderTarget> Map, Deep[2];
};

struct CachedGeometry
{
    std::weak_ptr<jRenderObjectGeometryData> Source;
    std::shared_ptr<jRenderObject> Object;
    std::size_t InputTriangles = 0, OutputTriangles = 0, Bytes = 0;
    uint64 LastUsed = 0;
};
std::map<std::string, CachedGeometry> GeometryCache;
std::size_t GeometryCacheBytes = 0;
uint64 CacheClock = 0;
struct Bounds
{
    std::weak_ptr<jVertexStreamData> Source;
    Math::Vec3 Minimum, Maximum;
};
std::map<const jVertexStreamData*, Bounds> BoundsCache;

void TrimCache()
{
    std::erase_if(GeometryCache, [](const auto& pair)
    {
        if (!pair.second.Source.expired()) return false;
        GeometryCacheBytes -= pair.second.Bytes;
        return true;
    });
    while (GeometryCache.size() > 32 || GeometryCacheBytes > GeometryCacheBudget)
    {
        auto oldest = std::min_element(GeometryCache.begin(), GeometryCache.end(),
            [](const auto& a, const auto& b) { return a.second.LastUsed < b.second.LastUsed; });
        if (oldest == GeometryCache.end()) break;
        GeometryCacheBytes -= oldest->second.Bytes;
        GeometryCache.erase(oldest);
    }
}

bool ExtractWorldMesh(const jRenderObject& object, Math::TriangleMesh& output,
    std::size_t maximumTriangles, std::string& error)
{
    const auto& geometry = object.GeometryDataPtr;
    if (!geometry || object.HasInstancing() || geometry->IndirectCommandBufferPtr
        || object.GetPrimitiveType() != EPrimitiveType::TRIANGLES)
    { error = "Advanced shadow geometry requires non-instanced triangles"; return false; }
    auto stream = geometry->VertexStream_PositionOnlyPtr;
    if (!stream || stream->Params.empty() || stream->ElementCount <= 0)
    { error = "Advanced shadow geometry has no retained CPU positions"; return false; }
    const auto& parameter = stream->Params.front();
    const auto stride = parameter->Stride > 0 ? std::size_t(parameter->Stride) : sizeof(float) * 3;
    const auto count = std::size_t(stream->ElementCount);
    if (stride < sizeof(float) * 3 ||
        parameter->GetBufferSize() < (count - 1) * stride + sizeof(float) * 3 ||
        parameter->Attributes.empty() || parameter->Attributes.front().UnderlyingType != EBufferElementType::FLOAT)
    { error = "Advanced shadow position layout or vertex budget is invalid"; return false; }
    const auto* bytes = static_cast<const unsigned char*>(parameter->GetBufferData());
    Math::TriangleMesh candidate;
    std::vector<uint32> sourceIndices;
    const auto* element = dynamic_cast<const jRenderObjectElement*>(&object);
    std::size_t firstVertex = 0, vertexCount = count;
    if (element)
    {
        const auto& submesh = element->SubMesh;
        if (submesh.StartVertex < 0 || submesh.EndVertex < submesh.StartVertex
            || std::size_t(submesh.EndVertex) > count)
        { error = "Advanced shadow submesh vertex range is invalid"; return false; }
        firstVertex = std::size_t(submesh.StartVertex);
        vertexCount = std::size_t(submesh.EndVertex - submesh.StartVertex);
    }
    if (geometry->IndexStreamPtr && geometry->IndexStreamPtr->Param)
    {
        const auto& index = *geometry->IndexStreamPtr;
        const auto size = index.Param->GetElementSize();
        std::size_t firstIndex = 0, indexCount = index.ElementCount;
        if (element)
        {
            const auto& submesh = element->SubMesh;
            if (submesh.StartFace < 0 || submesh.EndFace < submesh.StartFace
                || std::size_t(submesh.EndFace) > indexCount)
            { error = "Advanced shadow submesh index range is invalid"; return false; }
            firstIndex = std::size_t(submesh.StartFace);
            indexCount = std::size_t(submesh.EndFace - submesh.StartFace);
        }
        if (!indexCount || indexCount % 3 || indexCount / 3 > maximumTriangles ||
            (size != 1 && size != 2 && size != 4) ||
            index.Param->GetBufferSize() < std::size_t(index.ElementCount) * size)
        { error = "Advanced shadow triangle/index budget or index layout is invalid"; return false; }
        const auto* source = static_cast<const unsigned char*>(index.Param->GetBufferData());
        sourceIndices.reserve(indexCount);
        for (std::size_t i = 0; i < indexCount; ++i)
        {
            uint32 value = 0;
            std::memcpy(&value, source + (firstIndex + i) * size, size);
            // Match jRenderObjectElement::Draw's base-vertex offset. A shared
            // imported buffer must not duplicate every submesh for each material.
            if (value >= vertexCount || std::size_t(value) + firstVertex >= count)
            { error = "Advanced shadow submesh index is out of range"; return false; }
            sourceIndices.push_back(uint32(std::size_t(value) + firstVertex));
        }
    }
    else
    {
        if (!vertexCount || vertexCount % 3 || vertexCount / 3 > maximumTriangles)
        { error = "Advanced shadow nonindexed triangle budget is invalid"; return false; }
        for (std::size_t i = 0; i < vertexCount; ++i) sourceIndices.push_back(uint32(firstVertex + i));
    }
    std::map<uint32, uint32> compact;
    candidate.Indices.reserve(sourceIndices.size());
    for (uint32 original : sourceIndices)
    {
        auto found = compact.find(original);
        if (found == compact.end())
        {
            float xyz[3]; std::memcpy(xyz, bytes + std::size_t(original) * stride, sizeof(xyz));
            const Vector point = object.World.TransformPoint(Vector(xyz[0], xyz[1], xyz[2]));
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            { error = "Advanced shadow geometry contains nonfinite world positions"; return false; }
            const auto local = uint32(candidate.Positions.size());
            candidate.Positions.push_back(ToVec(point));
            found = compact.emplace(original, local).first;
        }
        candidate.Indices.push_back(found->second);
    }
    if (object.World.GetDeterminant() < 0)
        for (std::size_t i = 0; i < candidate.Indices.size(); i += 3)
            std::swap(candidate.Indices[i + 1], candidate.Indices[i + 2]);
    output = std::move(candidate);
    return true;
}

std::shared_ptr<jRenderObject> MakeGeometry(const std::vector<Math::Vec3>& positions,
    const std::vector<uint32>& indices, const std::shared_ptr<jMaterial>& material)
{
    if (positions.empty() || indices.empty()) return {};
    auto vertex = std::make_shared<jVertexStreamData>();
    auto parameter = std::make_shared<jStreamParam<float>>();
    parameter->Name = jNameStatic("AdvancedShadowPosition");
    parameter->Stride = sizeof(float) * 3;
    parameter->Attributes = {{jNameStatic("Position"), EBufferElementType::FLOAT, sizeof(float) * 3}};
    parameter->Data.reserve(positions.size() * 3);
    for (const auto& point : positions)
        for (double value : point) parameter->Data.push_back(static_cast<float>(value));
    vertex->Params.push_back(parameter);
    vertex->ElementCount = int32(positions.size());
    auto index = std::make_shared<jIndexStreamData>();
    auto* elements = new jStreamParam<uint32>();
    elements->Name = jNameStatic("AdvancedShadowIndex");
    elements->Stride = sizeof(uint32);
    elements->Attributes = {{jNameStatic("Index"), EBufferElementType::UINT32, sizeof(uint32)}};
    elements->Data = indices;
    index->Param = elements;
    index->ElementCount = uint32(indices.size());
    auto geometry = std::make_shared<jRenderObjectGeometryData>();
    geometry->VertexStreamPtr = geometry->VertexStream_PositionOnlyPtr = vertex;
    geometry->IndexStreamPtr = index;
    geometry->bHasVertexColor = false; geometry->bHasVertexBiTangent = false;
    // Dynamic shadow geometry is a bounded host-visible cache. Reuse the
    // existing completed-buffer attachment API, avoiding synchronous staging
    // copies/queue-idle waits when a moving light invalidates its geometry.
    auto vertexStorage = g_rhi->CreateRawBuffer(parameter->GetBufferSize(), 0,
        EBufferCreateFlag::CPUAccess | EBufferCreateFlag::VertexBuffer, EResourceLayout::GENERAL,
        parameter->GetBufferData(), parameter->GetBufferSize(), jNameStatic("AdvancedShadowVertices"));
    auto indexStorage = g_rhi->CreateRawBuffer(elements->GetBufferSize(), 0,
        EBufferCreateFlag::CPUAccess | EBufferCreateFlag::IndexBuffer, EResourceLayout::GENERAL,
        elements->GetBufferData(), elements->GetBufferSize(), jNameStatic("AdvancedShadowIndices"));
    if (!vertexStorage || !indexStorage) return {};
    if (IsUseVulkan())
    {
        auto attachedVertex = std::make_shared<jVertexBuffer_Vulkan>();
        if (!attachedVertex->InitializeWithBuffers(vertex, {std::static_pointer_cast<jBuffer_Vulkan>(vertexStorage)})) return {};
        auto attachedIndex = std::make_shared<jIndexBuffer_Vulkan>();
        attachedIndex->IndexStreamData = index;
        attachedIndex->BufferPtr = std::static_pointer_cast<jBuffer_Vulkan>(indexStorage);
        geometry->VertexBufferPtr = geometry->VertexBuffer_PositionOnlyPtr = attachedVertex;
        geometry->IndexBufferPtr = attachedIndex;
    }
#ifdef _WIN32
    else
    {
        auto attachedVertex = std::make_shared<jVertexBuffer_DX12>();
        if (!attachedVertex->InitializeWithBuffers(vertex, {std::static_pointer_cast<jBuffer_DX12>(vertexStorage)})) return {};
        auto attachedIndex = std::make_shared<jIndexBuffer_DX12>();
        attachedIndex->IndexStreamData = index;
        attachedIndex->BufferPtr = std::static_pointer_cast<jBuffer_DX12>(indexStorage);
        attachedIndex->IBView.BufferLocation = attachedIndex->BufferPtr->GetGPUAddress();
        attachedIndex->IBView.SizeInBytes = static_cast<UINT>(elements->GetBufferSize());
        attachedIndex->IBView.Format = DXGI_FORMAT_R32_UINT;
        geometry->VertexBufferPtr = geometry->VertexBuffer_PositionOnlyPtr = attachedVertex;
        geometry->IndexBufferPtr = attachedIndex;
    }
#endif
    if (!geometry->VertexBuffer_PositionOnlyPtr || !geometry->IndexBufferPtr) return {};
    auto object = std::make_shared<jRenderObject>();
    object->CreateRenderObject(geometry);
    object->MaterialPtr = material;
    Matrix identity; identity.SetIdentity();
    object->SetWorldMatrixOverride(identity);
    return object;
}

std::string GeometryKey(const jRenderObject& object, const jLight& light, const Settings& settings, bool volume)
{
    std::string key;
    const void* source = object.GeometryDataPtr.get();
    const auto append = [&](const auto& value)
    { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    append(source);
    append(light.Type);
    append(object.RenderObjectID);
    if (const auto* element = dynamic_cast<const jRenderObjectElement*>(&object))
    {
        append(element->SubMesh.StartVertex); append(element->SubMesh.EndVertex);
        append(element->SubMesh.StartFace); append(element->SubMesh.EndFace);
    }
    append(object.World);
    append(volume);
    const auto* vertex = object.GeometryDataPtr->VertexStream_PositionOnlyPtr.get();
    const auto* index = object.GeometryDataPtr->IndexStreamPtr.get();
    append(vertex); append(index);
    const auto position = light.GetPosition(), direction = light.GetDirection();
    append(position); append(direction);
    append(settings.VolumeRange); append(settings.VolumeMaxTriangles);
    const auto materialRevision = object.MaterialPtr ? object.MaterialPtr->Revision : 0;
    const auto* material = object.MaterialPtr.get(); append(material);
    append(materialRevision);
    return key;
}
}

struct Frame::Impl
{
    std::map<const jLight*, Output> Outputs;
    std::vector<std::shared_ptr<IUniformBufferBlock>> Uniforms;
    std::vector<std::shared_ptr<jRenderObject>> Geometry;
    std::set<const jRenderObject*> WorldSpaceGeometry;
    std::size_t GeometryWork = 0;
    std::size_t GeometryOutputWork = 0;

    bool PositiveCasterDomain(const Math::Mat4& projection)
    {
        for (const auto* object : jObject::GetShadowCasterRenderObject())
        {
            if (!object || !object->GeometryDataPtr) continue;
            if (object->MaterialPtr && object->MaterialPtr->Water) return false;
            const auto stream = object->GeometryDataPtr->VertexStream_PositionOnlyPtr;
            if (!stream || stream->Params.empty() || stream->ElementCount <= 0) return false;
            auto bounds = BoundsCache.find(stream.get());
            if (bounds == BoundsCache.end() || bounds->second.Source.expired())
            {
                const auto& parameter = stream->Params.front();
                const auto count = std::size_t(stream->ElementCount);
                const auto stride = parameter->Stride > 0 ? std::size_t(parameter->Stride) : sizeof(float) * 3;
                if (count > MaximumFrameInputTriangles * 3 || count / 3 > MaximumFrameInputTriangles - GeometryWork
                    || stride < sizeof(float) * 3 || parameter->Attributes.empty()
                    || parameter->Attributes.front().UnderlyingType != EBufferElementType::FLOAT
                    || parameter->GetBufferSize() < (count - 1) * stride + sizeof(float) * 3) return false;
                GeometryWork += count / 3;
                Bounds entry;
                entry.Source = stream;
                entry.Minimum.fill(std::numeric_limits<double>::max());
                entry.Maximum.fill(std::numeric_limits<double>::lowest());
                const auto* bytes = static_cast<const unsigned char*>(parameter->GetBufferData());
                for (std::size_t i = 0; i < count; ++i)
                {
                    float xyz[3]; std::memcpy(xyz, bytes + i * stride, sizeof(xyz));
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (!std::isfinite(xyz[axis])) return false;
                        entry.Minimum[axis] = std::min(entry.Minimum[axis], double(xyz[axis]));
                        entry.Maximum[axis] = std::max(entry.Maximum[axis], double(xyz[axis]));
                    }
                }
                if (BoundsCache.size() >= 256) BoundsCache.erase(BoundsCache.begin());
                bounds = BoundsCache.insert_or_assign(stream.get(), std::move(entry)).first;
            }
            const auto transform = Math::Multiply(projection, ToMath(object->World));
            for (int corner = 0; corner < 8; ++corner)
            {
                Math::Vec4 local;
                for (int axis = 0; axis < 3; ++axis)
                    local[axis] = (corner & (1 << axis)) ? bounds->second.Maximum[axis] : bounds->second.Minimum[axis];
                local[3] = 1;
                const auto clip = Math::Transform(transform, local);
                if (!std::isfinite(clip[3]) || clip[3] <= 1e-6) return false;
            }
        }
        return true;
    }

    template<typename T>
    std::shared_ptr<IUniformBufferBlock> Uniform(const char* name, const T& value)
    {
        auto buffer = g_rhi->CreateUniformBufferBlock(jName(name), jLifeTimeType::OneFrame, sizeof(T));
        if (buffer) { buffer->UpdateBufferData(&value, sizeof(T)); Uniforms.push_back(buffer); }
        return buffer;
    }

    bool ProjectionFor(const jView& view, const jLight& light, const Settings& settings,
        jAdvancedShadowData& value, FrameStats::Light& stats, std::string& error)
    {
        for (auto& matrix : value.ShadowVP) matrix.SetIdentity();
        const auto& camera = *view.Camera;
        const auto direction = camera.GetForwardVector().GetNormalize();
        value.CameraPositionAndType = Vector4(camera.Pos, float(Kind(light.Type)));
        value.CameraForwardAndOpacity = Vector4(direction, settings.DeepOpacityMultiplier);
        value.FilterParams = Vector4(settings.MinimumVariance, settings.BleedReduction, .001f, float(settings.BlurRadius));
        value.FarAndDeepParams = Vector4(settings.MaxDistance, settings.PositiveExponent, settings.NegativeExponent, settings.DeepTauMax);
        value.LightPositionAndNear = Vector4(light.GetPosition(), .01f);
        value.ModeParams = Vector4(float(settings.Method), 0, 1, 0);
        value.AtlasParams = Vector4(1, 1, float(settings.MapResolution), .02f);
        jShadowFiltering::Settings filtering;
        jShadowFiltering::TryMakeSettings(gOptions.ShadowFilterMode, gOptions.ShadowFilterRadiusTexels, filtering);
        value.SamplingParams = Vector4(float(filtering.Method), filtering.RadiusTexels, 1,
            settings.Method == Representation::Deep ? settings.DeepMaxDistance : settings.MaxDistance);
        const float maximumDistance = settings.Method == Representation::Deep ? settings.DeepMaxDistance : settings.MaxDistance;
        if (light.Type == ELightType::DIRECTIONAL)
        {
            if (settings.DirectionalProjection == Projection::Single)
                value.ShadowVP[0] = light.GetLightCamra()->Projection * light.GetLightCamra()->View;
            else
            {
                const double farDistance = std::min(double(camera.Far), double(maximumDistance));
                if (!(camera.Near > 0) || farDistance <= camera.Near)
                { error = "Directional shadow distance must exceed the view near plane"; return false; }
                Vector corners[8]; camera.GetFrustumVertexInWorld(corners); // Engine returns far first.
                Math::FrustumCorners frustum;
                const double fraction = (farDistance - camera.Near) / (camera.Far - camera.Near);
                for (int i = 0; i < 4; ++i)
                {
                    frustum[i] = ToVec(corners[i + 4]);
                    for (int axis = 0; axis < 3; ++axis)
                        frustum[i + 4][axis] = frustum[i][axis] + fraction * (ToVec(corners[i])[axis] - frustum[i][axis]);
                }
                if (settings.DirectionalProjection == Projection::Cascaded)
                {
                    Math::CascadeInput input;
                    input.Corners = frustum; input.NearDistance = camera.Near; input.FarDistance = farDistance;
                    input.LightDirection = ToVec(light.GetDirection()); input.Count = settings.CascadeCount;
                    input.Resolution = settings.MapResolution; input.SplitLambda = settings.SplitLambda;
                    input.CasterPadding = settings.CasterPadding; input.Stabilize = settings.CascadeStabilize;
                    Math::CascadeResult result;
                    if (!Math::BuildCascades(input, result, error)) return false;
                    value.ModeParams.y = 1; value.ModeParams.z = float(result.Count);
                    for (uint32 i = 0; i < result.Count; ++i)
                    { value.ShadowVP[i] = ToMatrix(result.Cascades[i].WorldToShadow); value.CascadeSplits.v[i] = float(result.Splits[i + 1]); }
                    value.AtlasParams.x = 2; value.AtlasParams.y = 2;
                }
                else
                {
                    Math::PerspectiveInput input;
                    input.Corners = frustum; input.CameraViewProjection = ToMath(camera.ViewProjection);
                    input.LightDirection = ToVec(light.GetDirection()); input.CasterPadding = settings.CasterPadding;
                    // Source PSM's virtual-camera slide avoids spending nearly
                    // all texels on an empty near plane. It changes neither the
                    // real camera nor the original receiver world frustum.
                    if (camera.IsPerspectiveProjection && PositiveCasterDomain(Math::Identity()))
                    {
                        double nearest = std::numeric_limits<double>::max();
                        for (const auto* caster : jObject::GetShadowCasterRenderObject())
                        {
                            if (!caster || !caster->GeometryDataPtr) continue;
                            const auto stream = caster->GeometryDataPtr->VertexStream_PositionOnlyPtr;
                            const auto bounds = BoundsCache.find(stream.get());
                            if (bounds == BoundsCache.end()) continue;
                            for (int corner = 0; corner < 8; ++corner)
                            {
                                Vector local;
                                for (int axis = 0; axis < 3; ++axis) local.v[axis] = float((corner & (1 << axis))
                                    ? bounds->second.Maximum[axis] : bounds->second.Minimum[axis]);
                                const auto world = caster->World.TransformPoint(local);
                                nearest = std::min(nearest, double((world - camera.Pos).DotProduct(direction)));
                            }
                        }
                        if (std::isfinite(nearest) && nearest < std::numeric_limits<double>::max())
                            input.VirtualSlideBack = std::clamp(nearest - camera.Near, 0.0, 1.0e6);
                    }
                    Math::PerspectiveResult result;
                    if (!Math::BuildPerspectiveShadowMap(input, result, error)) return false;
                    if (!result.OrthographicFallback && !PositiveCasterDomain(result.WorldToShadow))
                    {
                        Math::CascadeInput fallback;
                        fallback.Corners = frustum; fallback.NearDistance = camera.Near; fallback.FarDistance = farDistance;
                        fallback.LightDirection = input.LightDirection; fallback.Count = 1;
                        fallback.Resolution = settings.MapResolution; fallback.CasterPadding = settings.CasterPadding;
                        fallback.Stabilize = settings.CascadeStabilize;
                        Math::CascadeResult fitted;
                        if (!Math::BuildCascades(fallback, fitted, error)) return false;
                        result.WorldToShadow = fitted.Cascades[0].WorldToShadow;
                        result.OrthographicFallback = true;
                        result.FallbackReason = "PSM caster crosses a projective singularity or preparation budget; using fitted orthographic shadows";
                    }
                    value.ShadowVP[0] = ToMatrix(result.WorldToShadow); value.ModeParams.y = 2;
                    stats.ProjectionFallback = result.OrthographicFallback;
                    if (result.OrthographicFallback) stats.Diagnostic = result.FallbackReason;
                }
            }
        }
        else if (light.Type == ELightType::POINT && settings.PointProjection == PointProjection::DualParaboloid)
        {
            value.ModeParams.y = 3; value.ModeParams.z = 2;
            value.AtlasParams.x = 2;
            value.LightPositionAndNear.w = std::max(.01f, light.GetLightCamra()->Near);
            value.FarAndDeepParams.x = std::min(maximumDistance, light.GetLightCamra()->Far);
            if (value.FarAndDeepParams.x <= value.LightPositionAndNear.w)
            { error = "Dual-paraboloid shadow distance must exceed the light near plane"; return false; }
        }
        else
        {
            const int count = light.Type == ELightType::POINT ? 6 : 1;
            for (int i = 0; i < count; ++i)
            {
                const auto* lightCamera = light.GetLightCamra(i);
                // GetLightCamra returns the regular camera as well as its explicit
                // reverse-Z alternative. New encoders consistently use regular Z.
                value.ShadowVP[i] = lightCamera->Projection * lightCamera->View;
            }
            value.LightPositionAndNear.w = light.GetLightCamra()->Near;
            value.FarAndDeepParams.x = light.GetLightCamra()->Far;
            if (count == 6)
            { value.ModeParams.y = 4; value.ModeParams.z = 6; value.AtlasParams.x = 3; value.AtlasParams.y = 2; }
        }
        const int width = int(value.AtlasParams.x) * settings.MapResolution;
        const int height = int(value.AtlasParams.y) * settings.MapResolution;
        value.MapSizeAndInvSize = Vector4(float(width), float(height), 1.f / width, 1.f / height);
        stats.AtlasWidth = width; stats.AtlasHeight = height;
        return true;
    }

    std::shared_ptr<jRenderTarget> Target(const char* name, ETextureFormat format, int width, int height,
        const jRTClearValue& clear, bool storage = false)
    {
        const auto flags = (IsDepthFormat(format) ? ETextureCreateFlag::DSV
            : (ETextureCreateFlag::RTV | (storage ? ETextureCreateFlag::UAV : ETextureCreateFlag::NONE)))
            | ETextureCreateFlag::TransferSrc;
        return jRenderTargetPool::GetRenderTargetForOneFrame({
            .Type = ETextureType::TEXTURE_2D, .Format = format, .Width = width, .Height = height,
            .LayerCount = 1, .SampleCount = EMSAASamples::COUNT_1, .RTClearValue = clear,
            .TextureCreateFlag = flags, .ResourceName = jName(name)});
    }

    jRenderPass* RenderPass(const std::vector<std::shared_ptr<jRenderTarget>>& colors,
        const std::shared_ptr<jRenderTarget>& depth, int width, int height, bool loadDepth = false)
    {
        jRenderPassInfo info;
        info.SynchronizeDepthStencil = bool(depth);
        jSubpass subpass;
        for (const auto& color : colors)
        {
            subpass.OutputColorAttachments.push_back(int(info.Attachments.size()));
            info.Attachments.push_back({.RenderTargetPtr = color,
                .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
                .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
                .RTClearValue = color->Info.RTClearValue,
                .InitialLayout = EResourceLayout::COLOR_ATTACHMENT,
                .FinalLayout = EResourceLayout::COLOR_ATTACHMENT});
        }
        if (depth)
        {
            subpass.OutputDepthAttachment = int(info.Attachments.size());
            info.Attachments.push_back({.RenderTargetPtr = depth,
                .LoadStoreOp = loadDepth ? EAttachmentLoadStoreOp::LOAD_STORE : EAttachmentLoadStoreOp::CLEAR_STORE,
                .StencilLoadStoreOp = loadDepth ? EAttachmentLoadStoreOp::LOAD_STORE : EAttachmentLoadStoreOp::CLEAR_STORE,
                .RTClearValue = jRTClearValue(1.f, 0),
                .InitialLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT,
                .FinalLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT});
        }
        info.Subpasses.push_back(subpass);
        return g_rhi->GetOrCreateRenderPass(info, {0, 0}, {width, height});
    }

    jGraphicsPipelineShader RasterShaders(bool deep, bool volume)
    {
        jGraphicsPipelineShader shaders;
        for (int stage = 0; stage < 2; ++stage)
        {
            jShaderInfo info;
            const std::string name = std::string("AdvancedShadow") + (stage ? "PS" : "VS")
                + (deep ? "Deep" : "Depth") + (volume ? "Volume" : "Mesh");
            info.SetName(jName(name));
            info.SetShaderFilepath(stage ? jNameStatic("Resource/Shaders/hlsl/advanced_shadow_fs.hlsl")
                : jNameStatic("Resource/Shaders/hlsl/advanced_shadow_vs.hlsl"));
            info.SetShaderType(stage ? EShaderAccessStageFlag::FRAGMENT : EShaderAccessStageFlag::VERTEX);
            info.SetEntryPoint(jNameStatic("main"));
            info.SetPreProcessors(jName(std::string("#define JE_ADVANCED_SHADOW_DEEP ") + (deep ? "1\n" : "0\n")
                + "#define JE_ADVANCED_SHADOW_VOLUME " + (volume ? "1\n" : "0\n")));
            jShaderParameterSet::AppendToShaderInfo<jAdvancedShadowRasterParameters>(info, 0);
            if (!volume)
            {
                jShaderParameterSet::AppendToShaderInfo<jRenderObjectShaderParameters>(info, 1);
                jShaderParameterSet::AppendToShaderInfo<jMaterialShaderParameters>(info, 2);
            }
            auto* shader = g_rhi->CreateShader(info);
            if (stage) shaders.PixelShader = shader; else shaders.VertexShader = shader;
        }
        return shaders;
    }

    struct PreparedRaster
    {
        std::vector<jPipelineStateFixedInfo> States;
        std::vector<jDrawCommand> Draws;
    };

    bool PrepareRaster(PreparedRaster& prepared, const std::shared_ptr<jRenderFrameContext>& frame,
        jRenderPass* pass, jAdvancedShadowData value, const std::vector<jRenderObject*>& objects,
        bool deep, bool volume, jDepthStencilStateInfo* customDepth = nullptr, int heightOverride = 0)
    {
        const auto shaders = RasterShaders(deep, volume);
        if (!shaders.VertexShader || !shaders.PixelShader || !shaders.VertexShader->GetCompiledShader()
            || !shaders.PixelShader->GetCompiledShader()) return false;
        const bool clampDepth = volume || int(value.ModeParams.y) == 1 || int(value.ModeParams.y) == 2;
        auto* raster = clampDepth
            ? TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE, EFrontFace::CCW,
                false, 0.f, 0.f, 0.f, 1.f, true>::Create()
            : TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE>::Create();
        auto* depth = customDepth ? customDepth : deep
            ? TDepthStencilStateInfo<false, false>::Create()
            : TDepthStencilStateInfo<true, true, ECompareOp::LEQUAL>::Create();
        auto* blend = deep ? TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE,
            EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create()
            : TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO,
                EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();
        const int drawCount = int(value.ModeParams.z);
        prepared.States.reserve(drawCount);
        prepared.Draws.reserve(std::size_t(drawCount) * objects.size());
        for (int tile = 0; tile < drawCount; ++tile)
        {
            const int tileSize = int(value.AtlasParams.z), columns = int(value.AtlasParams.x);
            const int tileHeight = heightOverride ? heightOverride : tileSize;
            const int x = (tile % columns) * tileSize, y = (tile / columns) * tileSize;
            prepared.States.emplace_back(raster, depth, blend,
                jViewport(x, y, tileSize, tileHeight), jScissor(x, y, tileSize, tileHeight), false);
            value.ModeParams.w = float(tile);
            jAdvancedShadowRasterParameters parameters;
            parameters.AdvancedShadow.Buffer = Uniform("AdvancedShadowDraw", value);
            auto binding = jShaderParameterSet::CreateShaderBindingInstance(parameters,
                EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::SingleFrame);
            if (!binding) return false;
            for (auto* object : objects)
            {
                if (!object || !object->GeometryDataPtr || !object->GeometryDataPtr->VertexBuffer_PositionOnlyPtr) continue;
                if (object->HasInstancing()) return false;
                auto* material = object->MaterialPtr ? object->MaterialPtr.get() : GDefaultMaterial.get();
                jShaderBindingInstanceGroup bindings;
                bindings.Add(binding);
                if (!volume)
                {
                    if (WorldSpaceGeometry.contains(object))
                    {
                        // These position-only clones are already world-space.
                        // Do not create the normal object's unrelated RT test
                        // buffer, which would stage a synchronous GPU upload.
                        RenderObjectUniformBuffer objectValue{};
                        objectValue.M.SetIdentity(); objectValue.InvM.SetIdentity();
                        objectValue.BaseColorOpacity = material && material->UseMaterialProperties
                            ? material->BaseColorOpacity : Vector4(1.f);
                        objectValue.IndexElementSize = sizeof(uint32);
                        jRenderObjectShaderParameters objectParameters;
                        objectParameters.RenderObjectParam.Buffer = Uniform("AdvancedShadowWorldObject", objectValue);
                        bindings.Add(jShaderParameterSet::CreateShaderBindingInstance(objectParameters,
                            EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::SingleFrame));
                    }
                    else bindings.Add(object->CreateShaderBindingInstance());
                    bindings.Add(material->CreateShaderBindingInstance());
                }
                prepared.Draws.emplace_back(frame, object, pass, shaders, &prepared.States.back(), material,
                    bindings, nullptr, nullptr, 0, EDrawCommandBindingMode::Manual);
                prepared.Draws.back().PrepareToDraw(true);
                if (!prepared.Draws.back().CurrentPipelineStateInfo) return false;
            }
        }
        return true;
    }

    bool RecordRaster(const std::shared_ptr<jRenderFrameContext>& frame, jRenderPass* pass,
        PreparedRaster& prepared, const std::vector<std::shared_ptr<jRenderTarget>>& colors,
        const std::shared_ptr<jRenderTarget>& depth)
    {
        auto* command = frame->GetActiveCommandBuffer();
        for (const auto& target : colors)
            g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        if (depth) g_rhi->TransitionLayout(command, depth->GetTexture(), EResourceLayout::DEPTH_STENCIL_ATTACHMENT);
        Flush(frame);
        if (!pass || !pass->BeginRenderPass(command)) return false;
        for (const auto& draw : prepared.Draws) draw.Draw();
        pass->EndRenderPass();
        return true;
    }

    bool Compute(const std::shared_ptr<jRenderFrameContext>& frame, const jAdvancedShadowData& value,
        jTexture* input, jTexture* output, int pass)
    {
        jShaderInfo info;
        info.SetName(jName(std::string("AdvancedShadowCompute") + std::to_string(pass)));
        info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/advanced_shadow_cs.hlsl"));
        info.SetEntryPoint(jNameStatic("main"));
        info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        info.SetPreProcessors(jName(std::string("#define JE_ADVANCED_SHADOW_PASS ") + std::to_string(pass) + "\n"));
        jShaderParameterSet::AppendToShaderInfo<jAdvancedShadowComputeParameters>(info, 0);
        auto* shader = g_rhi->CreateShader(info);
        if (!shader || !shader->GetCompiledShader()) return false;
        jAdvancedShadowComputeParameters parameters;
        parameters.AdvancedShadow.Buffer = Uniform("AdvancedShadowCompute", value);
        parameters.ShadowInput = {input}; parameters.ShadowResult = {output};
        const auto binding = jShaderParameterSet::CreateShaderBindingInstance(parameters,
            EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);
        if (!binding) return false;
        jShaderBindingInstanceGroup group; group.Add(binding);
        auto* pipeline = g_rhi->CreateComputePipelineStateInfo(shader, group.GetLayoutArray(), {});
        if (!pipeline) return false;
        auto* command = frame->GetActiveCommandBuffer();
        g_rhi->TransitionLayout(command, input, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(command, output, EResourceLayout::GENERAL);
        Flush(frame);
        pipeline->Bind(frame);
        g_rhi->BindComputeShaderBindingInstances(command, pipeline, group.GetCombiner(), 0);
        g_rhi->DispatchCompute(frame, (uint32(value.MapSizeAndInvSize.x) + 7) / 8,
            (uint32(value.MapSizeAndInvSize.y) + 7) / 8, 1);
        return true;
    }

    bool ProcessGeometry(const jLight& light, const Settings& settings, bool volume,
        const std::vector<jRenderObject*>& source, std::vector<jRenderObject*>& objects,
        FrameStats::Light& stats, std::string& error)
    {
        struct TrimOnExit { ~TrimOnExit() { TrimCache(); } } trimOnExit;
        std::size_t inputTriangles = 0;
        for (auto* object : source)
        {
            if (!object || !object->GeometryDataPtr) continue;
            if (volume && object->Owner && object->Owner->SkipUpdateShadowVolume) continue;
            if (volume && object->MaterialPtr && object->MaterialPtr->UseMaterialProperties
                && (object->MaterialPtr->BaseColorOpacity.w < .999f ||
                    (object->MaterialPtr->Surface && object->MaterialPtr->Surface->AlphaMode != jSurfaceAlphaMode::Opaque)))
            { error = "Shadow volumes require opaque closed casters; use Deep for translucent surfaces"; return false; }
            if (object->MaterialPtr && object->MaterialPtr->Water)
            { error = volume ? "Shadow volumes require closed static geometry; Water uses depth shadows"
                : "Dual-paraboloid subdivision requires static geometry; Water uses cube shadows"; return false; }
            const auto key = GeometryKey(*object, light, settings, volume);
            auto cached = GeometryCache.find(key);
            if (cached == GeometryCache.end())
            {
                Math::TriangleMesh mesh;
                if (inputTriangles >= settings.VolumeMaxTriangles ||
                    !ExtractWorldMesh(*object, mesh, settings.VolumeMaxTriangles - inputTriangles, error))
                { if (error.empty()) error = "Advanced shadow per-light input triangle budget exceeded"; return false; }
                const auto count = mesh.Indices.size() / 3;
                if (GeometryWork + count > MaximumFrameInputTriangles)
                { error = "Advanced shadow CPU preparation exceeded the per-view triangle budget"; return false; }
                GeometryWork += count;
                CachedGeometry generated;
                generated.Source = object->GeometryDataPtr;
                generated.InputTriangles = count;
                if (volume)
                {
                    // Native LH primitives and imported RH meshes can use
                    // opposite consistent windings. Normalize a complete
                    // shell before applying the strict outward-volume builder.
                    const auto origin = mesh.Positions.front();
                    double signedVolume6 = 0;
                    for (std::size_t i = 0; i < mesh.Indices.size(); i += 3)
                    {
                        Math::Vec3 a, b, c;
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            a[axis] = mesh.Positions[mesh.Indices[i]][axis] - origin[axis];
                            b[axis] = mesh.Positions[mesh.Indices[i + 1]][axis] - origin[axis];
                            c[axis] = mesh.Positions[mesh.Indices[i + 2]][axis] - origin[axis];
                        }
                        signedVolume6 += a[0] * (b[1] * c[2] - b[2] * c[1])
                            + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
                    }
                    if (signedVolume6 < 0)
                        for (std::size_t i = 0; i < mesh.Indices.size(); i += 3)
                            std::swap(mesh.Indices[i + 1], mesh.Indices[i + 2]);
                    Math::VolumeInput input;
                    input.Positions = mesh.Positions; input.Indices = mesh.Indices;
                    input.PointLight = light.Type != ELightType::DIRECTIONAL;
                    input.Light = ToVec(input.PointLight ? light.GetPosition() : light.GetDirection());
                    input.ExtrusionDistance = settings.VolumeRange;
                    Math::VolumeLimits limits;
                    limits.MaxInputTriangles = settings.VolumeMaxTriangles;
                    limits.MaxOutputTriangles = std::min<std::size_t>(524288, settings.VolumeMaxTriangles * 8);
                    Math::VolumeMesh result;
                    if (!Math::BuildShadowVolume(input, result, error)) return false;
                    generated.OutputTriangles = result.Indices.size() / 3;
                    if (generated.OutputTriangles > MaximumFrameOutputTriangles - GeometryOutputWork)
                    { error = "Advanced shadow per-view output geometry budget exceeded"; return false; }
                    generated.Bytes = result.Positions.size() * sizeof(float) * 9 + result.Indices.size() * sizeof(uint32) * 2 + 4096;
                    if (!result.Indices.empty()) generated.Object = MakeGeometry(result.Positions, result.Indices, {});
                }
                else
                {
                    const auto position = light.GetPosition();
                    for (auto& point : mesh.Positions)
                    { point[0] -= position.x; point[1] -= position.y; point[2] -= position.z; }
                    Math::SubdivisionResult result;
                    if (!Math::SubdivideForDualParaboloid(mesh.Positions, mesh.Indices, result, error)) return false;
                    for (auto& point : result.Mesh.Positions)
                    { point[0] += position.x; point[1] += position.y; point[2] += position.z; }
                    generated.OutputTriangles = result.Mesh.Indices.size() / 3;
                    if (generated.OutputTriangles > MaximumFrameOutputTriangles - GeometryOutputWork)
                    { error = "Advanced shadow per-view output geometry budget exceeded"; return false; }
                    generated.Bytes = result.Mesh.Positions.size() * sizeof(float) * 9 + result.Mesh.Indices.size() * sizeof(uint32) * 2 + 4096;
                    generated.Object = MakeGeometry(result.Mesh.Positions, result.Mesh.Indices, object->MaterialPtr);
                }
                if (generated.OutputTriangles && !generated.Object)
                { error = "Advanced shadow GPU geometry allocation failed"; return false; }
                GeometryCacheBytes += generated.Bytes;
                cached = GeometryCache.emplace(key, std::move(generated)).first;
            }
            auto& item = cached->second;
            item.LastUsed = ++CacheClock;
            inputTriangles += item.InputTriangles;
            if (inputTriangles > settings.VolumeMaxTriangles)
            { error = "Advanced shadow per-light input triangle budget exceeded"; return false; }
            if (item.OutputTriangles > MaximumFrameOutputTriangles - GeometryOutputWork)
            { error = "Advanced shadow per-view output geometry budget exceeded"; return false; }
            GeometryOutputWork += item.OutputTriangles;
            stats.OutputTriangles += item.OutputTriangles;
            if (item.Object)
            {
                Geometry.push_back(item.Object); WorldSpaceGeometry.insert(item.Object.get());
                objects.push_back(item.Object.get());
            }
            TrimCache();
        }
        stats.InputTriangles = inputTriangles;
        TrimCache();
        return true;
    }

    bool RecordMaps(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view,
        const jLight& light, const Settings& settings, Output& output, FrameStats::Light& stats, std::string& error)
    {
        auto& value = output.Values;
        const int width = stats.AtlasWidth, height = stats.AtlasHeight;
        const bool deep = settings.Method == Representation::Deep;
        std::vector<jRenderObject*> objects = jObject::GetShadowCasterRenderObject();
        if (int(value.ModeParams.y) == 3)
        {
            std::vector<jRenderObject*> subdivided;
            if (!ProcessGeometry(light, settings, false, objects, subdivided, stats, error)) return false;
            objects = std::move(subdivided);
        }
        std::shared_ptr<jRenderTarget> depth, scratch;
        std::vector<std::shared_ptr<jRenderTarget>> colors;
        if (deep)
        {
            for (int i = 0; i < 2; ++i)
            {
                output.Deep[i] = Target(i ? "AdvancedDeepBins1" : "AdvancedDeepBins0",
                    ETextureFormat::RGBA16F, width, height, jRTClearValue(0.f, 0.f, 0.f, 0.f));
                if (!output.Deep[i]) { error = "Deep shadow target allocation failed"; return false; }
                colors.push_back(output.Deep[i]);
            }
            output.Map = output.Deep[0]; // Valid unused normal texture in the common layout.
        }
        else
        {
            depth = Target("AdvancedShadowDepth", ETextureFormat::D32, width, height, jRTClearValue(1.f, 0));
            output.Map = Target("AdvancedShadowAtlas", ETextureFormat::RGBA32F, width, height,
                jRTClearValue(1.f, 0.f, 0.f, 1.f), true);
            if (!depth || !output.Map) { error = "Advanced shadow target allocation failed"; return false; }
            if (settings.Method != Representation::Depth && settings.BlurRadius)
            {
                scratch = Target("AdvancedShadowBlur", ETextureFormat::RGBA32F, width, height,
                    jRTClearValue(0.f, 0.f, 0.f, 0.f), true);
                if (!scratch) { error = "Advanced shadow blur allocation failed"; return false; }
            }
        }
        auto* pass = RenderPass(colors, depth, width, height);
        PreparedRaster prepared;
        if (!pass || !PrepareRaster(prepared, frame, pass, value, objects, deep, false))
        { error = "Advanced shadow raster shader/pipeline preparation failed"; return false; }
        if (!RecordRaster(frame, pass, prepared, colors, depth))
        { error = "Advanced shadow raster recording failed"; return false; }
        stats.RasterPasses += int(value.ModeParams.z);
        if (!deep)
        {
            if (!Compute(frame, value, depth->GetTexture(), output.Map->GetTexture(), 0))
            { error = "Advanced shadow moment/depth encoding failed"; return false; }
            ++stats.ComputePasses;
            if (scratch)
            {
                if (!Compute(frame, value, output.Map->GetTexture(), scratch->GetTexture(), 1)
                    || !Compute(frame, value, scratch->GetTexture(), output.Map->GetTexture(), 2))
                { error = "Advanced shadow moment blur failed"; return false; }
                stats.ComputePasses += 2;
            }
        }
        auto* command = frame->GetActiveCommandBuffer();
        g_rhi->TransitionLayout(command, output.Map->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        for (const auto& target : output.Deep)
            if (target) g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        Flush(frame);
        return true;
    }

    bool RecordVolume(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view,
        const jLight& light, const Settings& settings, Output& output, FrameStats::Light& stats, std::string& error)
    {
        std::vector<jRenderObject*> volumes;
        if (!ProcessGeometry(light, settings, true, jObject::GetShadowCasterRenderObject(), volumes, stats, error)) return false;
        const int width = frame->SceneRenderTargetPtr->ColorPtr->Info.Width;
        const int height = frame->SceneRenderTargetPtr->ColorPtr->Info.Height;
        auto depth = Target("ShadowVolumeReceiverDepth", ETextureFormat::D32_S8,
            width, height, jRTClearValue(1.f, 0));
        output.Map = Target("ShadowVolumeVisibility", ETextureFormat::RGBA16F,
            width, height, jRTClearValue(1.f, 1.f, 1.f, 1.f));
        if (!depth || !output.Map) { error = "Shadow volume depth/stencil target allocation failed"; return false; }
        jAdvancedShadowData drawValue = output.Values;
        drawValue.ShadowVP[0] = view.Camera->ViewProjection;
        drawValue.ModeParams = Vector4(float(Representation::Depth), 0, 1, 0);
        drawValue.AtlasParams = Vector4(1, 1, float(width), 0);
        drawValue.MapSizeAndInvSize = Vector4(float(width), float(height), 1.f / width, 1.f / height);

        // Depth prepass precedes BasePass in both Forward and subpass Deferred,
        // avoiding a forbidden feedback dependency on the base depth attachment.
        auto* receiverPass = RenderPass({}, depth, width, height);
        PreparedRaster receivers;
        if (!receiverPass || !PrepareRaster(receivers, frame, receiverPass, drawValue,
            jObject::GetStaticRenderObject(), false, false, nullptr, height))
        { error = "Shadow volume receiver depth preparation failed"; return false; }
        if (!RecordRaster(frame, receiverPass, receivers, {}, depth))
        { error = "Shadow volume receiver depth recording failed"; return false; }
        ++stats.RasterPasses;

        auto* front = TStencilOpStateInfo<EStencilOp::KEEP, EStencilOp::KEEP, EStencilOp::DECR_WRAP,
            ECompareOp::ALWAYS, 0xff, 0xff, 0>::Create();
        auto* back = TStencilOpStateInfo<EStencilOp::KEEP, EStencilOp::KEEP, EStencilOp::INCR_WRAP,
            ECompareOp::ALWAYS, 0xff, 0xff, 0>::Create();
        jDepthStencilStateInfo stencilInfo;
        stencilInfo.DepthTestEnable = true; stencilInfo.DepthWriteEnable = false;
        stencilInfo.DepthCompareOp = ECompareOp::LEQUAL; stencilInfo.StencilTestEnable = true;
        stencilInfo.Front = front; stencilInfo.Back = back;
        auto* stencilState = g_rhi->CreateDepthStencilState(stencilInfo);
        auto* volumePass = RenderPass({output.Map}, depth, width, height, true);
        PreparedRaster volumeDraws;
        if (!volumePass || !PrepareRaster(volumeDraws, frame, volumePass, drawValue, volumes, false, true, stencilState, height))
        { error = "Shadow volume z-fail stencil preparation failed"; return false; }

        jGraphicsPipelineShader maskShaders;
        for (int stage = 0; stage < 2; ++stage)
        {
            jShaderInfo info;
            info.SetName(stage ? jNameStatic("ShadowVolumeMaskPS") : jNameStatic("ShadowVolumeMaskVS"));
            info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/advanced_shadow_volume_mask.hlsl"));
            info.SetEntryPoint(stage ? jNameStatic("PSMain") : jNameStatic("VSMain"));
            info.SetShaderType(stage ? EShaderAccessStageFlag::FRAGMENT : EShaderAccessStageFlag::VERTEX);
            auto* shader = g_rhi->CreateShader(info);
            if (stage) maskShaders.PixelShader = shader; else maskShaders.VertexShader = shader;
        }
        if (!maskShaders.VertexShader || !maskShaders.PixelShader || !maskShaders.VertexShader->GetCompiledShader()
            || !maskShaders.PixelShader->GetCompiledShader())
        { error = "Shadow volume visibility shader compilation failed"; return false; }
        auto* maskStencil = TStencilOpStateInfo<EStencilOp::KEEP, EStencilOp::KEEP, EStencilOp::KEEP,
            ECompareOp::NOTEQUAL, 0xff, 0, 0>::Create();
        jDepthStencilStateInfo maskDepth;
        maskDepth.DepthTestEnable = false; maskDepth.DepthWriteEnable = false;
        maskDepth.StencilTestEnable = true; maskDepth.Front = maskStencil; maskDepth.Back = maskStencil;
        jPipelineStateFixedInfo maskState(TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE>::Create(),
            g_rhi->CreateDepthStencilState(maskDepth), TBlendingStateInfo<false>::Create(),
            jViewport(0, 0, width, height), jScissor(0, 0, width, height), false);
        auto* maskPipeline = g_rhi->CreatePipelineStateInfo(&maskState, maskShaders, {}, volumePass, {}, nullptr, 0);
        if (!maskPipeline) { error = "Shadow volume visibility pipeline creation failed"; return false; }
        auto* command = frame->GetActiveCommandBuffer();
        g_rhi->TransitionLayout(command, output.Map->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        g_rhi->TransitionLayout(command, depth->GetTexture(), EResourceLayout::DEPTH_STENCIL_ATTACHMENT);
        Flush(frame);
        if (!volumePass->BeginRenderPass(command)) { error = "Shadow volume stencil recording failed"; return false; }
        for (const auto& draw : volumeDraws.Draws) draw.Draw();
        // DX12 must bind the resource-free root signature too; retaining the
        // preceding volume draw's uniform signature is incompatible with it.
        jShaderBindingInstanceGroup maskBindings;
        g_rhi->BindGraphicsShaderBindingInstances(command, maskPipeline, maskBindings.GetCombiner(), 0);
        maskPipeline->Bind(frame);
        g_rhi->DrawArrays(frame, EPrimitiveType::TRIANGLES, 0, 3);
        volumePass->EndRenderPass();
        ++stats.RasterPasses;
        g_rhi->TransitionLayout(command, output.Map->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        Flush(frame);
        stats.AtlasWidth = width; stats.AtlasHeight = height;
        return true;
    }
};

Frame::Frame() : Data(std::make_unique<Impl>()) {}
Frame::~Frame() = default;
const FrameStats& GetLastFrameStats() { return LastStats; }
bool GetLastDebugOutput(const jLight* light, jAdvancedShadowData& values,
    std::shared_ptr<jRenderTarget>& map, std::shared_ptr<jRenderTarget>& deep0,
    std::shared_ptr<jRenderTarget>& deep1)
{
    const auto it = DebugOutputs.find(light);
    if (it == DebugOutputs.end()) return false;
    auto target = it->second.Map.lock();
    if (!target) return false;
    values = it->second.Values; map = std::move(target);
    deep0 = it->second.Deep0.lock(); deep1 = it->second.Deep1.lock();
    return true;
}

void Frame::BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view, bool rasterShadows)
{
    LastStats = {};
    DebugOutputs.clear();
    if (!frame || !view.Camera) return;
    Neutral();
    TrimCache();
    std::set<const jLight*> currentLights;
    for (const auto& candidate : view.Lights) if (candidate.Light) currentLights.insert(candidate.Light);
    std::erase_if(LastDiagnostic, [&](const auto& entry) { return !currentLights.contains(entry.first); });
    bool reportedLightLimit = false;
    for (const auto& viewLight : view.Lights)
    {
        const auto* light = viewLight.Light;
        if (!light || (light->Type != ELightType::DIRECTIONAL && light->Type != ELightType::POINT
            && light->Type != ELightType::SPOT)) continue;
        const Settings requested = light->GetShadowOverride().value_or(gOptions.AdvancedShadows);
        if (!light->IsShadowCaster || !IsRequested(requested, light->Type)) continue;
        FrameStats::Light stats;
        stats.Source = light; stats.Requested = requested;
        if (LastStats.Lights.size() >= MaximumAdvancedLights)
        {
            if (!reportedLightLimit)
            {
                stats.Diagnostic = "Advanced shadows are limited to 16 lights per view; additional lights keep existing shadows";
                LastStats.Lights.push_back(std::move(stats)); reportedLightLimit = true;
            }
            continue;
        }
        if (!rasterShadows)
        {
            stats.Diagnostic = "HWRT direct lighting bypasses raster shadow-map methods";
            LastStats.Lights.push_back(std::move(stats));
            continue;
        }
        Settings settings;
        std::string error;
        Output output;
        bool valid = ValidateSettings(requested, settings, error)
            && ValidateForLight(settings, Kind(light->Type), error)
            && Data->ProjectionFor(view, *light, settings, output.Values, stats, error);
        if (valid)
        {
            std::size_t bytes = std::size_t(stats.AtlasWidth) * stats.AtlasHeight;
            if (settings.Method == Representation::ShadowVolume)
                bytes = std::size_t(frame->SceneRenderTargetPtr->ColorPtr->Info.Width)
                    * frame->SceneRenderTargetPtr->ColorPtr->Info.Height * 16;
            else if (settings.Method == Representation::Deep) bytes *= 16;
            else bytes *= 20 + ((settings.Method != Representation::Depth && settings.BlurRadius) ? 16 : 0);
            if (bytes > PerLightTargetBudget || bytes > PerViewTargetBudget - LastStats.AdditionalTargetBytes)
            { valid = false; error = "Advanced shadow target memory budget exceeded; keeping existing shadows"; }
            else
            {
                // Budget attempted allocations too: a failed transaction can have
                // already allocated one-frame targets that retire at frame end.
                LastStats.AdditionalTargetBytes += bytes; stats.AdditionalTargetBytes = bytes;
                valid = settings.Method == Representation::ShadowVolume
                    ? Data->RecordVolume(frame, view, *light, settings, output, stats, error)
                    : Data->RecordMaps(frame, view, *light, settings, output, stats, error);
            }
        }
        if (valid)
        {
            output.Uniform = Data->Uniform("AdvancedShadowLight", output.Values);
            valid = !!output.Uniform;
            if (!valid) error = "Advanced shadow uniform allocation failed";
        }
        stats.Active = valid;
        if (valid)
        {
            DebugOutputs.emplace(light, DebugOutput{output.Values, output.Map, output.Deep[0], output.Deep[1]});
            Data->Outputs.emplace(light, std::move(output));
        }
        else stats.Diagnostic = error.empty() ? "Advanced shadows unavailable; keeping existing shadows" : error;
        if (!stats.Diagnostic.empty() && LastDiagnostic[light] != stats.Diagnostic)
        {
            jLog::Get().Write(EjLogLevel::Warning, "Shadows", stats.Diagnostic);
            LastDiagnostic[light] = stats.Diagnostic;
        }
        else if (stats.Diagnostic.empty()) LastDiagnostic.erase(light);
        LastStats.Lights.push_back(std::move(stats));
    }
}

bool Frame::HasOutput(const jLight* light) const { return Data->Outputs.contains(light); }

#define JE_ADVANCED_LIGHT_BINDINGS(TYPE, PREFIX) \
void SetDefaultBindings(TYPE& p) \
{ \
    p.PREFIX##Shadow.Buffer = Neutral(); \
    p.PREFIX##AdvancedShadowMap = {GWhiteTexture.get(), AtlasSampler()}; \
    p.PREFIX##DeepShadow0 = {GWhiteTexture.get()}; \
    p.PREFIX##DeepShadow1 = {GWhiteTexture.get()}; \
} \
void Frame::SetBindings(TYPE& p, const jLight* light) const \
{ \
    SetDefaultBindings(p); \
    const auto it = Data->Outputs.find(light); \
    if (it == Data->Outputs.end()) return; \
    const auto& output = it->second; \
    p.PREFIX##Shadow.Buffer = output.Uniform; \
    p.PREFIX##AdvancedShadowMap = {output.Map->GetTexture(), AtlasSampler()}; \
    p.PREFIX##DeepShadow0 = {output.Deep[0] ? output.Deep[0]->GetTexture() : GWhiteTexture.get()}; \
    p.PREFIX##DeepShadow1 = {output.Deep[1] ? output.Deep[1]->GetTexture() : GWhiteTexture.get()}; \
}
JE_ADVANCED_LIGHT_BINDINGS(jDirectionalLightShaderParameters, Directional)
JE_ADVANCED_LIGHT_BINDINGS(jPointLightShaderParameters, Point)
JE_ADVANCED_LIGHT_BINDINGS(jSpotLightShaderParameters, Spot)
#undef JE_ADVANCED_LIGHT_BINDINGS

std::shared_ptr<jShaderBindingInstance> Frame::CreateLightBinding(jLight* light, jTexture* legacyShadow) const
{
    // Initialize the original light-only cache as well; legacy shadow draws keep
    // their exact projection/binding interface when an advanced mode falls back.
    const auto legacy = light->PrepareShaderBindingInstance(legacyShadow);
    if (!HasOutput(light)) return legacy;
    const auto* sampler = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR,
        ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER,
        ETextureAddressMode::CLAMP_TO_BORDER, 0.f, 1.f, Vector4(1.f), true, ECompareOp::LESS>::Create();
    auto* self = const_cast<Impl*>(Data.get());
    switch (light->Type)
    {
    case ELightType::DIRECTIONAL:
    {
        jDirectionalLightShaderParameters p;
        p.DirectionalLight.Buffer = self->Uniform("AdvancedDirectionalLight", static_cast<jDirectionalLight*>(light)->GetLightData());
        p.DirectionalLightShadowMap = {legacyShadow ? legacyShadow : GWhiteTexture.get(), sampler};
        SetBindings(p, light);
        return jShaderParameterSet::CreateShaderBindingInstance(p, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::SingleFrame);
    }
    case ELightType::POINT:
    {
        jPointLightShaderParameters p;
        p.PointLight.Buffer = self->Uniform("AdvancedPointLight", static_cast<jPointLight*>(light)->GetLightData());
        p.PointLightShadowCubeMap = {legacyShadow ? legacyShadow : GWhiteCubeTexture.get(), sampler};
        SetBindings(p, light);
        return jShaderParameterSet::CreateShaderBindingInstance(p, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::SingleFrame);
    }
    case ELightType::SPOT:
    {
        jSpotLightShaderParameters p;
        p.SpotLight.Buffer = self->Uniform("AdvancedSpotLight", static_cast<jSpotLight*>(light)->GetLightData());
        p.SpotLightShadowMap = {legacyShadow ? legacyShadow : GWhiteTexture.get(), sampler};
        SetBindings(p, light);
        return jShaderParameterSet::CreateShaderBindingInstance(p, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::SingleFrame);
    }
    default: return legacy;
    }
}
int RunAdapterTests()
{
    int failures = 0;
    const auto expect = [&](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << "advanced shadow adapter: " << name << '\n';
        failures += !pass;
    };
    // Two material submeshes share a strided position/index allocation. Only
    // the second triangle is drawn, with an independent base-vertex offset.
    auto stream = std::make_shared<jVertexStreamData>();
    auto positions = std::make_shared<jStreamParam<float>>();
    positions->Stride = 16;
    positions->Attributes = {{jNameStatic("Position"), EBufferElementType::FLOAT, 12}};
    positions->Data = {0,0,0,99, 1,0,0,99, 0,1,0,99,
        100,0,0,99, 101,0,0,99, 100,1,0,99};
    stream->Params.push_back(positions); stream->ElementCount = 6;
    auto index = std::make_shared<jIndexStreamData>();
    auto* indices = new jStreamParam<uint16>();
    indices->Data = {0,1,2,0,1,2};
    indices->Attributes = {{jNameStatic("Index"), EBufferElementType::UINT16, 2}};
    index->Param = indices; index->ElementCount = 6;
    auto geometry = std::make_shared<jRenderObjectGeometryData>();
    geometry->VertexStreamPtr = geometry->VertexStream_PositionOnlyPtr = stream;
    geometry->IndexStreamPtr = index;
    jRenderObjectElement object;
    object.GeometryDataPtr = geometry;
    object.World.SetIdentity(); object.World.m00 = -2; object.World.m11 = 3;
    object.World.m30 = 10; object.World.m31 = 20; object.World.m32 = 30;
    object.SubMesh.StartVertex = 3; object.SubMesh.EndVertex = 6;
    object.SubMesh.StartFace = 3; object.SubMesh.EndFace = 6;
    Math::TriangleMesh result;
    std::string error;
    expect(ExtractWorldMesh(object, result, 1, error), "reads a bounded real imported draw slice");
    expect(result.Positions == std::vector<Math::Vec3>{{-190,20,30},{-192,20,30},{-190,23,30}},
        "honors stride, nonzero base vertex, actual world transform and unused vertices");
    expect(result.Indices == std::vector<uint32>{0,2,1}, "compacts indices and preserves mirrored outward winding");
    const auto previousPositions = result.Positions; const auto previousIndices = result.Indices;
    object.SubMesh.EndFace = 9;
    expect(!ExtractWorldMesh(object, result, 1, error) && result.Positions == previousPositions
        && result.Indices == previousIndices, "invalid submesh bounds retain the prior output");
    object.SubMesh.EndFace = 6;
    indices->Data[3] = 3;
    expect(!ExtractWorldMesh(object, result, 1, error), "rejects a local index outside its submesh vertex range");
    indices->Data[3] = 0;
    expect(!ExtractWorldMesh(object, result, 0, error), "rejects input work before exceeding the triangle budget");
    indices->Data.clear(); index->ElementCount = 0;
    object.SubMesh.StartFace = 0; object.SubMesh.EndFace = 0;
    expect(!ExtractWorldMesh(object, result, 1, error), "rejects an empty index stream before reading its data");
    geometry->IndexStreamPtr.reset();
    expect(ExtractWorldMesh(object, result, 1, error) && result.Positions == previousPositions
        && result.Indices == previousIndices, "nonindexed submeshes use their actual vertex slice");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

void ReleaseResources()
{
    GeometryCache.clear(); GeometryCacheBytes = 0;
    BoundsCache.clear();
    NeutralUniform.reset(); LastDiagnostic.clear(); LastStats = {};
    DebugOutputs.clear();
}
}
