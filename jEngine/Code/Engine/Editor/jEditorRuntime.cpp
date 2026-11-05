#include "pch.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "jEditorRuntime.h"
#include "jEditorTools.h"
#include "FileLoader/jEditorAssetPath.h"
#include "FileLoader/jEditorAssets.h"
#include "jEditorProjectSettings.h"
#include "Code/Engine/jEditor.h"
#include "Scene/jObject.h"
#include "Scene/jMeshObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "FileLoader/jAssetImportService.h"
#include "FileLoader/jModelUploadSession.h"
#include "Material/jMaterial.h"
#include "RHI/jRaytracingScene.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include <set>

namespace
{
using namespace jEditorAuthoring;
Vector V(const Vec3& v) { return Vector(v.X, v.Y, v.Z); }
Vec3 A(const Vector& v) { return {v.x, v.y, v.z}; }
bool Finite(const Vector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Matrix Compose(const Transform& t)
{
    return Matrix::MakeTranslate(V(t.Position)) * Matrix::MakeRotateZ(DegreeToRadian(t.RotationDegrees.Z))
        * Matrix::MakeRotateY(DegreeToRadian(t.RotationDegrees.Y)) * Matrix::MakeRotateX(DegreeToRadian(t.RotationDegrees.X))
        * Matrix::MakeScale(V(t.Scale));
}
bool SameMatrix(const Matrix& a, const Matrix& b)
{
    for (int i = 0; i < 16; ++i) if (std::abs(a.mm[i] - b.mm[i]) > 0.00001f) return false;
    return true;
}
bool Decompose(const Matrix& m, Transform& t)
{
    Vector x(m.m00,m.m01,m.m02), y(m.m10,m.m11,m.m12), z(m.m20,m.m21,m.m22);
    const Vector scale(x.Length(), y.Length(), z.Length());
    if (!Finite(scale) || scale.x < 0.000001f || scale.y < 0.000001f || scale.z < 0.000001f) return false;
    x /= scale.x; y /= scale.y; z /= scale.z;
    if (std::abs(x.DotProduct(y)) > 0.002f || std::abs(x.DotProduct(z)) > 0.002f
        || std::abs(y.DotProduct(z)) > 0.002f || x.CrossProduct(y).DotProduct(z) < 0.99f) return false;
    const float ry = std::asin(std::clamp(-x.z, -1.0f, 1.0f));
    float rx = 0, rz = 0;
    if (std::abs(std::cos(ry)) > 0.00001f) { rx = std::atan2(y.z,z.z); rz = std::atan2(x.y,x.x); }
    else rz = std::atan2(-y.x,y.y);
    t.Position = {m.m30,m.m31,m.m32};
    t.RotationDegrees = {RadianToDegree(rx),RadianToDegree(ry),RadianToDegree(rz)};
    t.Scale = A(scale);
    return Finite(V(t.Position));
}
Transform LightTransform(jLight* light)
{
    Transform t;
    t.Position = A(light->GetPosition());
    const auto d = light->GetDirection().GetNormalize();
    if (light->SupportsDirection() && Finite(d))
        t.RotationDegrees = {RadianToDegree(std::asin(std::clamp(-d.z,-1.0f,1.0f))),0,RadianToDegree(std::atan2(d.x,-d.y))};
    return t;
}
EntityKind LightKind(jLight* light)
{
    switch(light->GetLightType()) {
    case ELightType::POINT:return EntityKind::PointLight;
    case ELightType::SPOT:return EntityKind::SpotLight;
    case ELightType::DIRECTIONAL:return EntityKind::DirectionalLight;
    default:return EntityKind::Empty;
    }
}
LightProperties ReadLight(jLight* light)
{
    LightProperties p; p.CastShadows=light->IsShadowCaster;
    if (auto* l=dynamic_cast<jPointLight*>(light)) { p.Color=A(l->GetLightData().Color);p.Range=l->GetLightData().MaxDistance; }
    if (auto* l=dynamic_cast<jSpotLight*>(light)) { const auto& d=l->GetLightData();p.Color=A(d.Color);p.Range=d.MaxDistance;p.InnerConeDegrees=RadianToDegree(d.PenumbraRadian);p.OuterConeDegrees=RadianToDegree(d.UmbraRadian); }
    if (auto* l=dynamic_cast<jDirectionalLight*>(light)) p.Color=A(l->GetLightData().Color);
    return p;
}
std::shared_ptr<jObject> CloneObject(const jObject& source)
{
    auto copy=std::make_shared<jObject>();
    copy->SkipShadowMapGen=source.SkipShadowMapGen;copy->SkipUpdateShadowVolume=source.SkipUpdateShadowVolume;
    copy->RenderObjectGeometryDataPtr=source.RenderObjectGeometryDataPtr;
    copy->BoundSphere=source.BoundSphere;
    for (auto* original:source.RenderObjects) {
        if (!original) continue;
        jRenderObject* mesh;
        if (auto* element=dynamic_cast<jRenderObjectElement*>(original)) {
            auto* e=new jRenderObjectElement(); e->SubMesh=element->SubMesh; mesh=e;
        } else mesh=new jRenderObject();
        mesh->GeometryDataPtr=original->GeometryDataPtr;mesh->MaterialPtr=original->MaterialPtr;
        mesh->IsTwoSided=original->IsTwoSided;mesh->IsHiddenBoundBox=original->IsHiddenBoundBox;
        original->UpdateWorldMatrix();
        mesh->SetPos(original->GetPos());mesh->SetRot(original->GetRot());mesh->SetScale(original->GetScale());
        mesh->SetWorldMatrixOverride(original->World);mesh->UpdateWorldMatrix();
        copy->AddRenderObject(mesh);
    }
    // A prototype/clone is not published yet. Transform setters can notify the
    // legacy AS tracker before registration; do not leave ghost prototype refs.
    if(g_rhi&&g_rhi->RaytracingScene)for(auto* mesh:copy->RenderObjects){
        auto* scene=g_rhi->RaytracingScene;scene->DirtyTransform.erase(mesh);scene->DirtyBLAS.erase(mesh);scene->PendingAdd.erase(mesh);scene->PendingRemove.erase(mesh);
    }
    return copy;
}
std::vector<Matrix> MeshMatrices(jObject* object)
{
    std::vector<Matrix> result;
    if (object) for (auto* mesh:object->RenderObjects) { mesh->UpdateWorldMatrix();result.push_back(mesh->World); }
    return result;
}
bool InScene(jObject* object)
{
    const auto& objects=jObject::GetStaticObject();return std::find(objects.begin(),objects.end(),object)!=objects.end();
}
bool InLights(jLight* light)
{
    const auto& lights=jLight::GetLights();return std::find(lights.begin(),lights.end(),light)!=lights.end();
}
}

struct jEditorRuntime::Impl
{
    using EntityId=jEditorAuthoring::EntityId;
    struct Source { std::string Path,Id;uint64_t Revision=0;jObject* Object=nullptr; };
    struct Binding {
        jObject* Object=nullptr;jLight* Light=nullptr;
        std::shared_ptr<jObject> OwnedObject;
        std::shared_ptr<jLight> OwnedLight;
        std::shared_ptr<jCamera> Camera;
        std::vector<Matrix> MeshLocal;
        jEditorAuthoring::Entity Last;
        Matrix AppliedWorld{IdentityType};
        bool Registered=false,MaterialApplied=false,HasAppliedWorld=false;
        std::string AssetKey;
    };
    struct Borrowed { jObject* Object=nullptr;jObjectID Id=0;jLight* Light=nullptr;bool WasRegistered=false; };
    struct Prototype { std::shared_ptr<jObject> Object;std::vector<Matrix> MeshLocal; };
    struct GraphDefaultsEntry
    {
        std::optional<MaterialProperties> Material;
        jEditorTools::PreparedMaterialMapping Mapping;
        bool Dirty = true, Failed = false, Pending = false;
    };
    struct Import { std::string Key,Source,Id;uint64_t Revision=0; };
    jEditor& Editor;
    const std::thread::id Thread=std::this_thread::get_id();
    jEditorAuthoring::Document Doc;
    std::filesystem::path Root=std::filesystem::current_path();
    jEditor::AssetResolver Resolver;
    jEditor::ProceduralFactory Factory;
    jEditor::ProceduralUpdater UpdateProcedural;
    jEditor::MaterialApplier ApplyMaterial;
    std::map<EntityId,Binding> Bindings;
    std::map<jObjectID,Source> Sources;
    std::map<std::string,Prototype> Prototypes;
    std::map<std::string, GraphDefaultsEntry> GraphDefaults;
    uint64_t MappingTextureEpoch = 0;
    std::map<std::string,uint64_t> Revisions;
    std::map<uint64_t,Import> Imports;
    struct Upload { Import Job; std::unique_ptr<jModelUploadSession> Session; };
    std::map<std::string,Upload> Uploads;
    std::set<std::string> FailedImports;
    std::map<EntityId,EntityId> DuplicateOf;
    std::vector<std::shared_ptr<jObject>> RetiredObjects;
    std::vector<std::shared_ptr<jLight>> RetiredLights;
    std::vector<Borrowed> BorrowedObjects;
    std::unique_ptr<jAssetImportService> Importer;
    std::vector<EntityId> Selected;
    std::array<bool,static_cast<size_t>(EInputKey::MAX)> Keys{};
    std::string Error,Status;
    uint64_t AppliedRevision=UINT64_MAX;
    bool Enabled=false,Captured=false,ShuttingDown=false,Hovered=false,Focused=false,ShortcutFocused=false,GizmoTransaction=false;
    bool RasterSurfacePublicationEnabled = true;
    float Time=0,MoveSpeed=200;
    Vector OrbitTarget=Vector::ZeroVector;
    jCamera* Camera=nullptr;
    struct CameraState {Vector Pos,Target,Up;float Fov,Near,Far,Distance;bool Valid=false;} SavedCamera{};

    explicit Impl(jEditor& editor):Editor(editor) {}
    bool Owner() const {return Thread==std::this_thread::get_id();}
    std::string Key(const std::string& source,const std::string& id) const {
        const auto revision=Revisions.find(id.empty()?source:id);
        return source+"|"+id+"|"+std::to_string(revision==Revisions.end()?0:revision->second);
    }
    std::filesystem::path Resolve(const std::string& source,const std::string& id)
    {
        std::filesystem::path path;
        try {
            path=Resolver?Resolver(id,source):Root/jEditorAssetPath::FromUtf8(source);
            if(path.empty())return {};
            path=std::filesystem::weakly_canonical(path);
            const auto base=std::filesystem::weakly_canonical(Root);
            const auto relative=path.lexically_relative(base);
            if(relative.empty() || relative.is_absolute())return {};
            for(const auto& part:relative)if(part=="..")return {};
            return path;
        }catch(...){return {};}
    }
    void Register(Binding& b,bool visible)
    {
        if(b.Registered==visible)return;
        if(b.Object){ if(visible)jObject::AddObject(b.Object);else jObject::RemoveObject(b.Object); }
        if(b.Light){ if(visible)jLight::AddLights(b.Light);else jLight::RemoveLights(b.Light); }
        b.Registered=visible;
    }
    void Retire(Binding& b)
    {
        Register(b,false);
        if(b.Light)Editor.Placement.UnregisterLight(b.Light);
        if(b.OwnedObject)RetiredObjects.push_back(std::move(b.OwnedObject));
        if(b.OwnedLight)RetiredLights.push_back(std::move(b.OwnedLight));
    }
    void CollectRetired(bool force=false)
    {
        if(RetiredObjects.empty()&&RetiredLights.empty())return;
        auto* scene=g_rhi?g_rhi->RaytracingScene:nullptr;
        auto referenced=[&](jObject* object){
            if(!scene)return false;
            for(auto* mesh:object->RenderObjects)
                if(scene->PendingRemove.count(mesh)||scene->PendingAdd.count(mesh)
                    ||std::find(scene->InstanceList.begin(),scene->InstanceList.end(),mesh)!=scene->InstanceList.end())return true;
            return false;
        };
        bool ready=force || !RetiredLights.empty();
        for(auto& object:RetiredObjects)if(!referenced(object.get()))ready=true;
        if(!ready)return;
        if(g_rhi)g_rhi->Flush();
        if(force&&scene)for(auto& object:RetiredObjects)for(auto* mesh:object->RenderObjects){
            scene->PendingRemove.erase(mesh);scene->PendingAdd.erase(mesh);scene->DirtyBLAS.erase(mesh);scene->DirtyTransform.erase(mesh);
            std::erase(scene->InstanceList,mesh);scene->ForceRebuildTLAS();
        }
        std::erase_if(RetiredObjects,[&](const auto& object){return force||!referenced(object.get());});
        RetiredLights.clear();
    }
    void RestoreBorrowed()
    {
        for(const auto& b:BorrowedObjects){
            if(b.Object&&jObject::FindObjectByID(b.Id)==b.Object&&b.WasRegistered&&!InScene(b.Object))jObject::AddObject(b.Object);
            if(b.Light&&b.WasRegistered&&!InLights(b.Light))jLight::AddLights(b.Light);
        }
        BorrowedObjects.clear();
    }
    Matrix World(EntityId id,std::map<EntityId,Matrix>& cache)
    {
        if(auto it=cache.find(id);it!=cache.end())return it->second;
        auto* entity=Doc.Find(id);if(!entity)return Matrix(IdentityType);
        Transform local=entity->Local;
        if(Doc.IsPlaying())Doc.EvaluateLocal(id,Time,local);
        Matrix result=Compose(local);
        if(entity->Parent)result=World(entity->Parent,cache)*result;
        cache[id]=result;return result;
    }
    void Apply(Binding& b,const Entity& entity,const Matrix& world)
    {
        if(b.Object){
            for(size_t i=0;i<b.Object->RenderObjects.size();++i){
                auto* mesh=b.Object->RenderObjects[i];
                const Matrix transformed=world*(i<b.MeshLocal.size()?b.MeshLocal[i]:Matrix(IdentityType));
                mesh->SetWorldMatrixOverride(transformed);mesh->UpdateWorldMatrix();
            }
            if(ApplyMaterial&&(!b.MaterialApplied || !(b.Last.Material==entity.Material))){
                if(entity.Material.OverrideEnabled||b.MaterialApplied){
                const auto resolved = Editor.ResolveMaterial(entity.Material);
                const auto graph = GraphDefaults.find(entity.Material.GraphAsset);
                if (entity.Material.OverrideEnabled && graph != GraphDefaults.end() && graph->second.Failed)
                    return; // Retain the previous material/resources until a valid reimport.
                if (entity.Material.OverrideEnabled && graph != GraphDefaults.end() && graph->second.Mapping.Program
                    && graph->second.Mapping.Program->Active && !RasterSurfacePublicationEnabled)
                { Error = "Mapping material publication requires Deferred or Forward; the current material was retained."; return; }
                for(auto* mesh:b.Object->RenderObjects)if(mesh->MaterialPtr&&mesh->MaterialPtr.use_count()>1){
                    if(auto source=std::dynamic_pointer_cast<jMeshMaterial>(mesh->MaterialPtr))mesh->MaterialPtr=std::make_shared<jMeshMaterial>(*source);
                    else mesh->MaterialPtr=std::make_shared<jMaterial>(*mesh->MaterialPtr);
                    mesh->MaterialPtr->ShaderBindingInstance.reset();mesh->MaterialPtr->NeedToUpdateShaderBindingInstance=true;
                }
                ApplyMaterial(resolved,*b.Object);
                if (entity.Material.OverrideEnabled)
                {
                    const jEditorTools::PreparedMaterialMapping empty;
                    const auto& prepared = graph != GraphDefaults.end() ? graph->second.Mapping : empty;
                    for (auto* mesh : b.Object->RenderObjects) if (mesh->MaterialPtr)
                        jEditorTools::ApplyMaterialMapping(*mesh->MaterialPtr, prepared, jEditorTools::MaterialMappingOverrides(entity.Material));
                }
                }
                b.MaterialApplied=true;
            }
        }
        if(b.Light){
            const bool refresh=!b.HasAppliedWorld||!SameMatrix(b.AppliedWorld,world)
                ||!(b.Last.Light==entity.Light)||bool(b.Light->PreUpdateLambda);
            const Vector position(world.m30,world.m31,world.m32);
            const Vector direction=world.TransformDirection(Vector(0,-1,0)).GetNormalize();
            b.Light->SetPosition(position);if(Finite(direction)&&!direction.IsNearlyZero())b.Light->SetDirection(direction);
            const Vector color=V(entity.Light.Color)*entity.Light.Intensity;
            if(auto* l=dynamic_cast<jPointLight*>(b.Light)){l->SetColor(color);l->SetMaxDistance(entity.Light.Range);}
            if(auto* l=dynamic_cast<jSpotLight*>(b.Light)){
                l->SetColor(color);l->SetMaxDistance(entity.Light.Range);l->SetConeAngles(DegreeToRadian(entity.Light.InnerConeDegrees),DegreeToRadian(entity.Light.OuterConeDegrees));
                // The legacy spotlight setter only changes LightData.Position.
                // Its shadow camera must move with the authored light before
                // Update rebuilds View/ShadowVP using the new direction.
                if(l->Camera){const Vector offset=position-l->Camera->Pos;l->Camera->Pos+=offset;l->Camera->Target+=offset;l->Camera->Up+=offset;}
            }
            if(auto* l=dynamic_cast<jDirectionalLight*>(b.Light))l->SetColor(color);
            b.Light->IsShadowCaster=entity.Light.CastShadows;
            if(refresh){
                // Publication follows Game.Update. Refresh shadow cameras and
                // light volumes now without invoking the legacy script twice.
                struct ScopedCallback {
                    jLight& Light;PRE_UPDATE_LAMBDA Saved;
                    explicit ScopedCallback(jLight& light):Light(light),Saved(std::move(light.PreUpdateLambda))
                    { Light.PreUpdateLambda={}; }
                    ~ScopedCallback(){Light.PreUpdateLambda=std::move(Saved);}
                } callback(*b.Light);
                b.Light->Update(0);
            }
        }
        b.AppliedWorld=world;b.HasAppliedWorld=true;
        b.Last=entity;
        Register(b,Doc.IsEffectivelyVisible(entity.Id));
    }
    bool Make(const Entity& entity,Binding& binding)
    {
        binding.Last=entity;
        if(entity.Kind==EntityKind::Model)binding.AssetKey=Key(entity.SourceAsset,entity.AssetId);
        const auto duplicate=DuplicateOf.find(entity.Id);
        if(duplicate!=DuplicateOf.end()&&(entity.Kind==EntityKind::Model||entity.Kind==EntityKind::Cube||entity.Kind==EntityKind::Sphere))if(auto original=Bindings.find(duplicate->second);original!=Bindings.end()&&original->second.Object){
            binding.OwnedObject=CloneObject(*original->second.Object);binding.Object=binding.OwnedObject.get();
            binding.MeshLocal=original->second.MeshLocal;binding.MaterialApplied=true;return true;
        }
        if(entity.Kind==EntityKind::PointLight || entity.Kind==EntityKind::SpotLight || entity.Kind==EntityKind::DirectionalLight){
            if(entity.Kind==EntityKind::PointLight)binding.OwnedLight.reset(jLight::CreatePointLight(Vector::ZeroVector,Vector::OneVector,entity.Light.Range,Vector::OneVector,Vector::OneVector,64));
            if(entity.Kind==EntityKind::SpotLight)binding.OwnedLight.reset(jLight::CreateSpotLight(Vector::ZeroVector,Vector(0,-1,0),Vector::OneVector,entity.Light.Range,DegreeToRadian(entity.Light.InnerConeDegrees),DegreeToRadian(entity.Light.OuterConeDegrees),Vector::OneVector,Vector::OneVector,64));
            if(entity.Kind==EntityKind::DirectionalLight)binding.OwnedLight.reset(jLight::CreateDirectionalLight(Vector(0,-1,0),Vector::OneVector,Vector::OneVector,Vector::OneVector,64));
            binding.Light=binding.OwnedLight.get();Editor.Placement.RegisterLight(binding.Light);return true;
        }
        if(entity.Kind==EntityKind::Empty)return true;
        if(entity.Kind==EntityKind::Camera){binding.Camera=std::make_shared<jCamera>();return true;}
        if(entity.Kind==EntityKind::Water&&!RasterSurfacePublicationEnabled){Error="Water publication requires Deferred or Forward; switch renderer to create its runtime surface.";return false;}
        if(entity.Kind==EntityKind::ParticleEmitter||entity.Kind==EntityKind::Terrain||entity.Kind==EntityKind::Water){
            if(!Factory){Error=std::string(KindName(entity.Kind))+" runtime factory is not installed.";return false;}
            binding.OwnedObject=Factory(entity,Error);binding.Object=binding.OwnedObject.get();
            if(!binding.Object)return false;
            binding.MeshLocal=MeshMatrices(binding.Object);return true;
        }
        const auto key=entity.Kind==EntityKind::Model?Key(entity.SourceAsset,entity.AssetId):std::string("builtin:")+KindName(entity.Kind);
        auto prototype=Prototypes.find(key);
        if(prototype==Prototypes.end()){
            if(entity.Kind==EntityKind::Model){
                if(entity.SourceAsset.empty()&&entity.AssetId.empty()){Error="Model has no reconstructible source asset.";return false;}
                if(FailedImports.count(key))return false;
                bool pending=Uploads.count(key)!=0;for(auto& [id,job]:Imports)pending|=job.Key==key;
                if(!pending){
                    const auto path=Resolve(entity.SourceAsset,entity.AssetId);
                    if(path.empty()||!std::filesystem::is_regular_file(path)){Error="Missing or out-of-project asset: "+entity.SourceAsset;FailedImports.insert(key);return false;}
                    if(!Importer)Importer=std::make_unique<jAssetImportService>();
                    const auto request=Importer->Request(jEditorAssetPath::ToUtf8(path),jEditorAssetPath::ToUtf8(path.parent_path()));
                    if(request)Imports[request]={key,entity.SourceAsset,entity.AssetId,0};
                }
                Status="Preparing model assets on CPU workers; GPU publication remains owner-thread work.";return false;
            }
            if(!g_rhi){Error="A graphics device is required to create primitive geometry.";return false;}
            std::shared_ptr<jObject> object;
            if(entity.Kind==EntityKind::Cube)object.reset(jPrimitiveUtil::CreateCube(Vector::ZeroVector,Vector(20),Vector::OneVector,Vector4::OneVector));
            if(entity.Kind==EntityKind::Sphere)object.reset(jPrimitiveUtil::CreateSphere(Vector::ZeroVector,15,32,16,Vector::OneVector,Vector4::OneVector,false,true));
            if(!object){Error="Unsupported runtime entity kind.";return false;}
            Prototypes[key]={object,MeshMatrices(object.get())};prototype=Prototypes.find(key);
        }
        if(entity.Kind==EntityKind::Model&&!entity.MeshLocalTransforms.empty()
            &&entity.MeshLocalTransforms.size()!=prototype->second.Object->RenderObjects.size()){
            Error="Model mesh count changed; clear its saved mesh-local transforms before replacing the existing instance.";return false;
        }
        binding.OwnedObject=CloneObject(*prototype->second.Object);binding.Object=binding.OwnedObject.get();binding.MeshLocal=prototype->second.MeshLocal;
        if(entity.Kind==EntityKind::Model&&!entity.MeshLocalTransforms.empty()){binding.MeshLocal.clear();for(const auto& transform:entity.MeshLocalTransforms)binding.MeshLocal.push_back(Compose(transform));}
        return true;
    }
    void Poll()
    {
        jModelUploadSession::CollectRetired();
        // One admission step globally per editor tick, with at most two payloads
        // held here. The import service's ready queue supplies backpressure.
        if(!Uploads.empty()){
            auto it=Uploads.begin();auto& upload=it->second;
            const auto progress=upload.Session->Step({8*1024*1024,2.0,2});
            Status="GPU upload "+std::to_string(progress.CompletedBytes)+" / "+std::to_string(progress.TotalBytes)+" bytes; worst admission "+std::to_string(progress.WorstStepMilliseconds)+" ms";
            if(progress.AdmissionOverrun)Status+="; allocation exceeded the admission time target";
            if(progress.Current==jModelUploadSession::State::Ready){
                std::shared_ptr<jObject> object(upload.Session->TakeModel().release());
                if(object){Prototypes[it->first]={object,MeshMatrices(object.get())};AppliedRevision=UINT64_MAX;Status="Model upload complete; publishing shared instances.";}
                else {Error="Upload reported ready without a model.";FailedImports.insert(it->first);}
                Uploads.erase(it);
            }else if(progress.Current==jModelUploadSession::State::Failed||progress.Current==jModelUploadSession::State::Cancelled){
                Error="Model upload failed: "+progress.Error;FailedImports.insert(it->first);Uploads.erase(it);
            }
        }
        if(!Importer||Uploads.size()>=2)return;
        auto result=Importer->Poll();if(!result)return;
        const auto found=Imports.find(result->Id);if(found==Imports.end())return;
        const auto job=found->second;Imports.erase(found);
        if(result->State!=jAssetImportService::Status::Ready){Error="Model preparation failed: "+result->Error;FailedImports.insert(job.Key);return;}
        if(!g_rhi){Error="Model publication requires a graphics device.";FailedImports.insert(job.Key);return;}
        std::string error;auto session=jModelLoader::GetInstance().BeginFinalize(std::move(result->Model),&error);
        if(!session){Error="Model upload could not start: "+error;FailedImports.insert(job.Key);return;}
        Uploads.emplace(job.Key,Upload{job,std::move(session)});
    }
    std::vector<EntityId> SelectionRoots() const
    {
        if(!Doc.SelectedId())return {};
        auto selected=Selected;
        if(std::find(selected.begin(),selected.end(),Doc.SelectedId())==selected.end())selected={Doc.SelectedId()};
        std::vector<EntityId> roots;
        for(auto id:selected){
            const auto* entity=Doc.Find(id);if(!entity)continue;bool ancestor=false;
            for(auto parent=entity->Parent;parent;){if(std::find(selected.begin(),selected.end(),parent)!=selected.end()){ancestor=true;break;}auto* p=Doc.Find(parent);parent=p?p->Parent:0;}
            if(!ancestor)roots.push_back(id);
        }
        return roots;
    }
};

jEditorRuntime::jEditorRuntime(jEditor& editor):Data(std::make_unique<Impl>(editor)){}
jEditorRuntime::~jEditorRuntime()=default;

jEditorAuthoring::Document& jEditor::GetDocument(){return Runtime->Data->Doc;}
const jEditorAuthoring::Document& jEditor::GetDocument()const{return Runtime->Data->Doc;}
void jEditor::SetProjectRoot(const std::filesystem::path& root)
{
    auto& s = *Runtime->Data;
    if (!s.Owner()) return;
    try
    {
        s.Root = std::filesystem::weakly_canonical(root);
        s.GraphDefaults.clear();
        for (auto& [id, binding] : s.Bindings) binding.MaterialApplied = false;
        s.AppliedRevision = UINT64_MAX;
    }
    catch (const std::exception& e) { s.Error = e.what(); }
}
const std::filesystem::path& jEditor::GetProjectRoot()const{return Runtime->Data->Root;}
void jEditor::SetAssetResolver(AssetResolver resolver){auto& s=*Runtime->Data;if(s.Owner())s.Resolver=std::move(resolver);}
void jEditor::SetProceduralFactory(ProceduralFactory factory,ProceduralUpdater update){auto& s=*Runtime->Data;if(!s.Owner())return;s.Factory=std::move(factory);s.UpdateProcedural=std::move(update);s.AppliedRevision=UINT64_MAX;}
void jEditor::SetMaterialApplier(MaterialApplier apply){auto& s=*Runtime->Data;if(!s.Owner())return;s.ApplyMaterial=std::move(apply);for(auto& [id,b]:s.Bindings)b.MaterialApplied=false;s.AppliedRevision=UINT64_MAX;}
MaterialProperties jEditor::ResolveMaterial(const MaterialProperties& material)
{
    auto& s = *Runtime->Data;
    if (!s.Owner() || material.GraphAsset.empty() || !material.OverrideEnabled) return material;
    auto& entry = s.GraphDefaults[material.GraphAsset];
    if (entry.Dirty)
    {
        const bool wasPending = entry.Pending;
        entry.Dirty = false;
        std::string error;
        const auto file = jEditorProject::ResolveSafe(s.Root, jEditorAssetPath::FromUtf8(material.GraphAsset), error, true);
        jEditorTools::MaterialGraph graph;
        MaterialProperties defaults;
        jEditorTools::PreparedMaterialMapping prepared;
        bool pending = false;
        if (file && jEditorTools::LoadMaterial(*file, graph, error) && jEditorTools::EvaluateMaterial(graph, defaults, error)
            && jEditorTools::PrepareMaterialMapping(s.Root, graph, prepared, error, &pending))
        {
            entry.Material = std::move(defaults); entry.Mapping = std::move(prepared); entry.Failed = false; entry.Pending = false;
            if (wasPending) s.Status = "Mapping material ready.";
            if (s.Error.starts_with(material.GraphAsset + ": ")) s.Error.clear();
        }
        else { entry.Failed = true; entry.Pending = pending; if (pending) s.Status = error; else { s.Error = material.GraphAsset + ": " + error; s.Status = "Mapping preparation failed; the prior material was retained."; } }
    }
    auto resolved = material;
    if (entry.Material) jEditorTools::InheritMaterialGraph(resolved, *entry.Material);
    return resolved;
}
void jEditor::SetAuthoringEnabled(bool enabled){auto& s=*Runtime->Data;if(!s.Owner()||s.Enabled==enabled)return;s.Enabled=enabled;for(size_t i=0;i<s.Keys.size();++i)s.Keys[i]=IsKeyDown(static_cast<EInputKey>(i));}
void jEditor::SetRasterSurfacePublicationEnabled(bool enabled)
{
    auto& s = *Runtime->Data;
    if (!s.Owner() || s.RasterSurfacePublicationEnabled == enabled) return;
    s.RasterSurfacePublicationEnabled = enabled;
    if (enabled && (s.Error.starts_with("Mapping material publication requires") || s.Error.starts_with("Water publication requires"))) s.Error.clear();
    for (auto& [id, binding] : s.Bindings) binding.MaterialApplied = false;
    s.AppliedRevision = UINT64_MAX;
}
bool jEditor::IsAuthoringEnabled()const{return Runtime->Data->Enabled;}
const std::string& jEditor::LastError()const{return Runtime->Data->Error;}
const std::string& jEditor::ImportStatus()const{return Runtime->Data->Status;}
size_t jEditor::PendingImportCount()const{return Runtime->Data->Imports.size()+Runtime->Data->Uploads.size()+jEditorTools::PendingMaterialMappingTextureCount();}
void jEditor::CancelPendingImports()
{
    auto& s=*Runtime->Data;if(!s.Owner())return;
    for(const auto& [id,job]:s.Imports)s.FailedImports.insert(job.Key);
    for(auto& [key,upload]:s.Uploads){s.FailedImports.insert(key);upload.Session->Cancel();}
    if(s.Importer)s.Importer->CancelAll();s.Imports.clear();s.Uploads.clear();
    jEditorTools::CancelMaterialMappingTexturePreparation();
    for (auto& [path, graph] : s.GraphDefaults) if (graph.Pending) { graph.Pending = false; graph.Failed = true; graph.Dirty = false; }
    s.Status="Imports cancelled; existing instances retained. Reimport the asset to retry.";
}
float jEditor::PlayTime()const{return Runtime->Data->Time;}

void jEditor::TrackWorldObjectSource(jObject* object,const std::string& source,const std::string& assetId,uint64_t revision)
{
    auto& s=*Runtime->Data;if(!s.Owner()||!object)return;
    std::filesystem::path path;
    try {
        path=jEditorAssetPath::FromUtf8(source);
        if(path.is_absolute())path=std::filesystem::relative(path,s.Root);
        else if(!std::filesystem::exists(s.Root/path)&&std::filesystem::exists(path))path=std::filesystem::relative(std::filesystem::absolute(path),s.Root);
    }catch(...){s.Error="Cannot make source asset project-relative.";return;}
    s.Sources[object->GetObjectID()]={jEditorAssetPath::ToUtf8(path),assetId,revision,object};
    s.Revisions[assetId.empty()?jEditorAssetPath::ToUtf8(path):assetId]=revision;
}

bool jEditor::CaptureWorldToDocument()
{
    auto& s=*Runtime->Data;if(!s.Owner()||s.ShuttingDown)return false;
    if(!s.Doc.Entities().empty()){s.Error="Capture requires an empty authored document; use a new scene first.";return false;}
    s.Error.clear();s.Doc.BeginTransaction("Capture current scene");
    const auto placed=Placement.PlacedObjects;
    const auto lights=jLight::GetLights();
    for(const auto& info:placed){
        if(info.Type==EPlacedObjectType::LIGHT)continue;
        auto* object=info.Object;if(!object||!InScene(object))continue;
        EntityKind kind=info.ShapeType==EPlacementShapeType::CUBE?EntityKind::Cube:info.ShapeType==EPlacementShapeType::SPHERE?EntityKind::Sphere:EntityKind::Model;
        const auto source=s.Sources.find(object->GetObjectID());
        if(kind==EntityKind::Model&&source==s.Sources.end()){s.Error="Some external objects have no source metadata and were not captured.";continue;}
        const auto id=s.Doc.Create(kind,kind==EntityKind::Model?jEditorAssetPath::ToUtf8(jEditorAssetPath::FromUtf8(source->second.Path).filename()):KindName(kind));
        if(!id)continue;
        auto entity=*s.Doc.Find(id);
        auto matrices=MeshMatrices(object);
        if(kind==EntityKind::Model){
            entity.SourceAsset=source->second.Path;entity.AssetId=source->second.Id;
            bool serializable=true;
            for(const auto& matrix:matrices){Transform transform;if(!Decompose(matrix,transform)){serializable=false;break;}entity.MeshLocalTransforms.push_back(transform);}
            if(!serializable){s.Error="An external model mesh cannot be represented as saved TRS; its object was not captured.";s.Doc.Delete(id);continue;}
        }
        else if(!matrices.empty()&&!Decompose(matrices[0],entity.Local)){
            s.Error="An external primitive transform cannot be represented as saved TRS; its object was not captured.";s.Doc.Delete(id);continue;
        }
        entity.Visible=object->Visible;entity.Material.Metallic=gOptions.Metallic;entity.Material.Roughness=gOptions.Roughness;
        if(!s.Doc.Update(entity,"Capture properties")){s.Error=s.Doc.LastError();s.Doc.Delete(id);continue;}
        jEditorRuntime::Impl::Binding b;b.Object=object;b.Last=entity;b.Registered=true;b.MaterialApplied=true;
        b.MeshLocal=kind==EntityKind::Model?matrices:std::vector<Matrix>(matrices.size(),Matrix(IdentityType));
        b.AssetKey=kind==EntityKind::Model?s.Key(entity.SourceAsset,entity.AssetId):std::string();
        s.Bindings[id]=b;s.BorrowedObjects.push_back({object,object->GetObjectID(),nullptr,true});
        const auto key=kind==EntityKind::Model?s.Key(entity.SourceAsset,entity.AssetId):std::string("builtin:")+KindName(kind);
        s.Prototypes[key]={CloneObject(*object),b.MeshLocal};
    }
    for(auto* light:lights){
        const auto kind=LightKind(light);if(kind==EntityKind::Empty)continue;
        const auto id=s.Doc.Create(kind,KindName(kind));if(!id)continue;
        auto entity=*s.Doc.Find(id);entity.Local=LightTransform(light);entity.Light=ReadLight(light);
        if(!s.Doc.Update(entity,"Capture light")){s.Error=s.Doc.LastError();s.Doc.Delete(id);continue;}
        jEditorRuntime::Impl::Binding b;b.Light=light;b.Registered=true;b.Last=entity;
        s.Bindings[id]=b;s.BorrowedObjects.push_back({nullptr,0,light,true});
    }
    s.Doc.CommitTransaction();s.Captured=true;s.AppliedRevision=UINT64_MAX;
    return true;
}

bool jEditor::SyncDocumentToWorld()
{
    auto& s=*Runtime->Data;if(!s.Owner()||s.ShuttingDown)return false;
    s.Poll();s.CollectRetired();
    const auto selectedBeforePublication=s.Doc.SelectedId();
    std::set<std::string> requiredAssets;
    for(const auto& [id,e]:s.Doc.Entities())if(e.Kind==EntityKind::Model)requiredAssets.insert(s.Key(e.SourceAsset,e.AssetId));
    for(auto it=s.Imports.begin();it!=s.Imports.end();)if(!requiredAssets.count(it->second.Key)){if(s.Importer)s.Importer->Cancel(it->first);it=s.Imports.erase(it);}else ++it;
    for(auto it=s.Uploads.begin();it!=s.Uploads.end();)if(!requiredAssets.count(it->first)){it->second.Session->Cancel();it=s.Uploads.erase(it);}else ++it;
    for(auto it=s.Bindings.begin();it!=s.Bindings.end();){
        const auto* entity=s.Doc.Find(it->first);
        if(!entity||entity->Kind!=it->second.Last.Kind||entity->SourceAsset!=it->second.Last.SourceAsset||entity->AssetId!=it->second.Last.AssetId){s.Retire(it->second);it=s.Bindings.erase(it);}else ++it;
    }
    bool complete=true;std::map<EntityId,Matrix> worlds;
    for(const auto& [id,entity]:s.Doc.Entities()){
        auto it=s.Bindings.find(id);
        if(it==s.Bindings.end()){
            jEditorRuntime::Impl::Binding b;
            if(!s.Make(entity,b)){complete=false;continue;}
            it=s.Bindings.emplace(id,std::move(b)).first;
        }
        if(entity.Kind==EntityKind::Terrain&&!(it->second.Last.Terrain==entity.Terrain)){
            jEditorRuntime::Impl::Binding replacement;
            if(s.Make(entity,replacement)){s.Retire(it->second);it->second=std::move(replacement);}else complete=false;
        }
        if(entity.Kind==EntityKind::Model&&it->second.AssetKey!=s.Key(entity.SourceAsset,entity.AssetId)){
            jEditorRuntime::Impl::Binding replacement;
            if(s.Make(entity,replacement)){s.Retire(it->second);it->second=std::move(replacement);}else complete=false;
        }
        if(entity.Kind==EntityKind::Model){
            if(!entity.MeshLocalTransforms.empty()){
                if(!it->second.Object||entity.MeshLocalTransforms.size()!=it->second.Object->RenderObjects.size()){
                    s.Error="Saved mesh-local transform count does not match the model; its existing live transform was preserved.";complete=false;continue;
                }
                it->second.MeshLocal.clear();for(const auto& transform:entity.MeshLocalTransforms)it->second.MeshLocal.push_back(Compose(transform));
            }else if(const auto prototype=s.Prototypes.find(it->second.AssetKey);prototype!=s.Prototypes.end())
                it->second.MeshLocal=prototype->second.MeshLocal;
        }
        s.Apply(it->second,entity,s.World(id,worlds));
    }
    s.Doc.Select(s.Doc.Find(selectedBeforePublication)?selectedBeforePublication:0);
    s.AppliedRevision=complete?s.Doc.Revision():UINT64_MAX;
    return complete;
}

void jEditor::ResetWorldBindings(bool newDocument)
{
    auto& s=*Runtime->Data;if(!s.Owner())return;
    if(s.Importer)s.Importer->CancelAll();s.Imports.clear();
    for(auto& [key,upload]:s.Uploads)upload.Session->Cancel();s.Uploads.clear();
    for(auto& [id,b]:s.Bindings)s.Retire(b);
    s.Bindings.clear();s.CollectRetired(true);s.RestoreBorrowed();
    if(g_rhi&&!s.Prototypes.empty())g_rhi->Flush();
    s.Sources.clear();s.Prototypes.clear();s.GraphDefaults.clear();s.DuplicateOf.clear();s.FailedImports.clear();s.Selected.clear();
    Placement.Clear();s.Captured=false;s.AppliedRevision=UINT64_MAX;s.Time=0;
    if(newDocument)s.Doc.NewScene();
}
void jEditor::ShutdownWorld()
{
    if(!Runtime)return;auto& s=*Runtime->Data;if(!s.Owner()||s.ShuttingDown)return;
    ResetWorldBindings(false);if(s.Importer){s.Importer->Shutdown();s.Importer.reset();}if(g_rhi)jModelUploadSession::DrainRetired();s.ShuttingDown=true;
}

jEditor::EntityId jEditor::EntityForObject(const jObject* object)const
{
    if(!object)return 0;for(const auto& [id,b]:Runtime->Data->Bindings)if(b.Object==object)return id;return 0;
}
jObject* jEditor::ObjectForEntity(EntityId id)const{const auto& b=Runtime->Data->Bindings;auto it=b.find(id);return it==b.end()?nullptr:it->second.Object;}
jLight* jEditor::LightForEntity(EntityId id)const{const auto& b=Runtime->Data->Bindings;auto it=b.find(id);return it==b.end()?nullptr:it->second.Light;}
jEditor::EntityId jEditor::GetSelectedEntityId()const{return GetDocument().SelectedId();}
const std::vector<jEditor::EntityId>& jEditor::GetSelectedEntities()const{return Runtime->Data->Selected;}
bool jEditor::SelectEntity(EntityId id,bool additive)
{
    auto& s=*Runtime->Data;if(!s.Owner()||!s.Doc.Select(id))return false;
    if(!additive)s.Selected.clear();if(id&&std::find(s.Selected.begin(),s.Selected.end(),id)==s.Selected.end())s.Selected.push_back(id);
    if(auto* object=ObjectForEntity(id))Placement.SelectObject(object);
    else {Placement.SelectPlacedObject(-1);if(auto* light=LightForEntity(id))for(int i=0;i<(int)Placement.PlacedObjects.size();++i)if(Placement.PlacedObjects[i].LightPtr==light){Placement.SelectedPlacedObjectIndex=i;break;}}
    return true;
}
void jEditor::SetSelectedEntities(const std::vector<EntityId>& ids)
{
    auto& s=*Runtime->Data;if(!s.Owner())return;s.Selected.clear();for(auto id:ids)if(s.Doc.Find(id)&&std::find(s.Selected.begin(),s.Selected.end(),id)==s.Selected.end())s.Selected.push_back(id);
    s.Doc.Select(s.Selected.empty()?0:s.Selected.back());
}
jEditor::EntityId jEditor::CreateEntity(EntityKind kind,const std::string& name,EntityId parent)
{
    auto& s=*Runtime->Data;if(!s.Owner())return 0;
    const bool transaction=s.Doc.BeginTransaction("Add "+std::string(KindName(kind)));
    const auto id=s.Doc.Create(kind,name,parent);
    if(id){auto entity=*s.Doc.Find(id);if(s.Camera&&!parent)entity.Local.Position=A(s.Camera->Pos+s.Camera->GetForwardVector()*100);
        if(kind==EntityKind::Cube||kind==EntityKind::Sphere||kind==EntityKind::Terrain||kind==EntityKind::ParticleEmitter||kind==EntityKind::Water)entity.Material.OverrideEnabled=true;
        if(kind==EntityKind::Water){entity.Material.BaseColor={.02f,.12f,.18f};entity.Material.Roughness=.08f;entity.Material.Metallic=0;entity.Material.Opacity=1;}
        s.Doc.Update(entity,"Place entity");}
    if(transaction)s.Doc.CommitTransaction();if(id)SelectEntity(id);else s.Error=s.Doc.LastError();return id;
}
jEditor::EntityId jEditor::AddModel(const std::string& source,const std::string& assetId,uint64_t revision)
{
    auto& s=*Runtime->Data;if(!s.Owner())return 0;
    std::filesystem::path sourcePath;
    try { sourcePath=jEditorAssetPath::FromUtf8(source); } catch(const std::exception& e) {s.Error=e.what();return 0;}
    const bool transaction=s.Doc.BeginTransaction("Add model");
    auto id=s.Doc.Create(EntityKind::Model,jEditorAssetPath::ToUtf8(sourcePath.filename()));
    if(id){auto entity=*s.Doc.Find(id);entity.SourceAsset=source;entity.AssetId=assetId;if(!s.Doc.Update(entity,"Set model source")){if(transaction)s.Doc.CancelTransaction();return 0;}}
    if(transaction)s.Doc.CommitTransaction();s.Revisions[assetId.empty()?source:assetId]=revision;SelectEntity(id);return id;
}
jEditor::EntityId jEditor::DuplicateSelected()
{
    auto& s=*Runtime->Data;if(!s.Owner())return 0;
    const auto roots=s.SelectionRoots();if(roots.empty())return 0;
    const bool transaction=s.Doc.BeginTransaction("Duplicate selection");std::vector<EntityId> copies;
    const auto mapCopies=[&](auto&& self,EntityId original,EntityId copy)->void {
        s.DuplicateOf[copy]=original;const auto oldChildren=s.Doc.Children(original),newChildren=s.Doc.Children(copy);
        for(size_t i=0;i<oldChildren.size()&&i<newChildren.size();++i)self(self,oldChildren[i],newChildren[i]);
    };
    for(auto root:roots){const auto copy=s.Doc.Duplicate(root);if(copy){mapCopies(mapCopies,root,copy);copies.push_back(copy);}}
    if(transaction)s.Doc.CommitTransaction();if(copies.empty()){s.Error=s.Doc.LastError();return 0;}SetSelectedEntities(copies);return copies.back();
}
bool jEditor::DeleteSelected()
{
    auto& s=*Runtime->Data;if(!s.Owner())return false;
    const auto roots=s.SelectionRoots();if(roots.empty())return false;
    const bool transaction=s.Doc.BeginTransaction("Delete selection");bool changed=false;
    for(auto id:roots)changed=s.Doc.Delete(id)||changed;
    if(transaction)s.Doc.CommitTransaction();if(changed)SelectEntity(0);return changed;
}
void jEditor::ReimportAsset(const std::string& source,uint64_t revision)
{
    auto& s=*Runtime->Data;if(!s.Owner())return;
    jEditorTools::InvalidateMaterialMappingTextureCache(s.Root, source);
    for(auto& [id,job]:s.Imports)if(job.Source==source&&s.Importer)s.Importer->Cancel(id);
    std::erase_if(s.Imports,[&](const auto& item){return item.second.Source==source;});
    for(auto it=s.Uploads.begin();it!=s.Uploads.end();)if(it->second.Job.Source==source){it->second.Session->Cancel();it=s.Uploads.erase(it);}else ++it;
    if(!revision){revision=s.Revisions[source];for(const auto& [id,e]:s.Doc.Entities())if(e.SourceAsset==source&&!e.AssetId.empty())revision=Max(revision,s.Revisions[e.AssetId]);++revision;}
    s.Revisions[source]=revision;
    const auto sourcePrefix=source+"|";
    std::erase_if(s.FailedImports,[&](const auto& key){return key.starts_with(sourcePrefix);});
    for(const auto& [id,entity]:s.Doc.Entities())if(entity.SourceAsset==source&&!entity.AssetId.empty())s.Revisions[entity.AssetId]=revision;
    if (const auto graph = s.GraphDefaults.find(source); graph != s.GraphDefaults.end()) graph->second.Dirty = true;
    for (auto& [path, graph] : s.GraphDefaults)
        if (graph.Mapping.Program && std::find(graph.Mapping.Program->Dependencies.begin(), graph.Mapping.Program->Dependencies.end(), source)
            != graph.Mapping.Program->Dependencies.end()) graph.Dirty = true;
    for (const auto& [id, entity] : s.Doc.Entities())
    {
        const auto binding = s.Bindings.find(id);
        if (binding == s.Bindings.end()) continue;
        const auto& material = entity.Material;
        const auto graph = s.GraphDefaults.find(material.GraphAsset);
        const bool mappingDependency = graph != s.GraphDefaults.end() && graph->second.Mapping.Program
            && std::find(graph->second.Mapping.Program->Dependencies.begin(), graph->second.Mapping.Program->Dependencies.end(), source)
                != graph->second.Mapping.Program->Dependencies.end();
        if ((!material.GraphAsset.empty() && material.GraphAsset == source) || mappingDependency)
        {
            binding->second.MaterialApplied = false;
            continue;
        }
        const auto resolved = ResolveMaterial(material);
        if (resolved.NormalTexture == source || resolved.BaseColorTexture == source)
            binding->second.MaterialApplied = false;
    }
    // Keep the last good instances until the new revision is fully prepared.
    s.AppliedRevision=UINT64_MAX;
}

bool jEditor::FocusSelected(jCamera* camera)
{
    auto& s=*Runtime->Data;if(!s.Owner()||!camera||!s.Doc.SelectedId())return false;
    Vector minimum(FLT_MAX),maximum(-FLT_MAX);bool any=false;
    std::map<const jRenderObjectGeometryData*,std::pair<Vector,Vector>> bounds;
    const auto roots=s.SelectionRoots();
    for(const auto& [id,entity]:s.Doc.Entities()){
        bool include=std::find(roots.begin(),roots.end(),id)!=roots.end();
        for(auto parent=entity.Parent;!include&&parent;){include=std::find(roots.begin(),roots.end(),parent)!=roots.end();const auto* p=s.Doc.Find(parent);parent=p?p->Parent:0;}
        if(!include)continue;
        auto it=s.Bindings.find(id);
        if(it!=s.Bindings.end()&&it->second.Object)for(auto* mesh:it->second.Object->RenderObjects){
            Vector lo(-10),hi(10);
            if(mesh->GeometryDataPtr&&mesh->GeometryDataPtr->VertexStreamPtr&&!mesh->GeometryDataPtr->VertexStreamPtr->Params.empty()){
                auto found=bounds.find(mesh->GeometryDataPtr.get());
                if(found==bounds.end()){
                    lo=Vector(FLT_MAX);hi=Vector(-FLT_MAX);const auto& vertices=mesh->GetVertices();
                    for(size_t n=0;n+2<vertices.size();n+=3){const Vector p(vertices[n],vertices[n+1],vertices[n+2]);if(!Finite(p))continue;lo=Vector(Min(lo.x,p.x),Min(lo.y,p.y),Min(lo.z,p.z));hi=Vector(Max(hi.x,p.x),Max(hi.y,p.y),Max(hi.z,p.z));}
                    if(lo.x>hi.x){lo=Vector(-10);hi=Vector(10);}bounds[mesh->GeometryDataPtr.get()]={lo,hi};
                }else{lo=found->second.first;hi=found->second.second;}
            }
            for(int corner=0;corner<8;++corner){const Vector p=mesh->World.TransformPoint(Vector(corner&1?hi.x:lo.x,corner&2?hi.y:lo.y,corner&4?hi.z:lo.z));if(!Finite(p))continue;minimum=Vector(Min(minimum.x,p.x),Min(minimum.y,p.y),Min(minimum.z,p.z));maximum=Vector(Max(maximum.x,p.x),Max(maximum.y,p.y),Max(maximum.z,p.z));any=true;}
        }
        else {float matrix[16];if(s.Doc.GetWorldMatrix(id,matrix)){const Vector p(matrix[12],matrix[13],matrix[14]);minimum=Vector(Min(minimum.x,p.x-10),Min(minimum.y,p.y-10),Min(minimum.z,p.z-10));maximum=Vector(Max(maximum.x,p.x+10),Max(maximum.y,p.y+10),Max(maximum.z,p.z+10));any=true;}}
    }
    if(!any)return false;
    const Vector center=(minimum+maximum)*0.5f;const float radius=Max(10.0f,(maximum-minimum).Length()*0.5f);
    const float distance=radius/Max(0.1f,std::tan(camera->FOVRad*0.5f))*1.25f;
    Vector forward=camera->GetForwardVector();if(!Finite(forward)||forward.IsNearlyZero())forward=Vector(0,0,1);
    const Vector position=center-forward.GetNormalize()*distance;
    jCamera::SetCamera(camera,position,center,position+Vector::UpVector,camera->FOVRad,camera->Near,Max(camera->Far,distance+radius*2),camera->Width,camera->Height,camera->IsPerspectiveProjection,distance);
    camera->UpdateCamera();s.OrbitTarget=center;return true;
}
bool jEditor::BeginPlay()
{
    auto& s=*Runtime->Data;if(!s.Owner()||!s.Doc.BeginPlay())return false;s.Time=0;
    if(s.Camera){auto* c=s.Camera;s.SavedCamera={c->Pos,c->Target,c->Up,c->FOVRad,c->Near,c->Far,c->Distance,true};}return true;
}
bool jEditor::StopPlay()
{
    auto& s=*Runtime->Data;if(!s.Owner()||!s.Doc.StopPlay())return false;
    if(s.Camera&&s.SavedCamera.Valid){const auto& c=s.SavedCamera;jCamera::SetCamera(s.Camera,c.Pos,c.Target,c.Up,c.Fov,c.Near,c.Far,s.Camera->Width,s.Camera->Height,s.Camera->IsPerspectiveProjection,c.Distance);s.Camera->UpdateCamera();}
    s.Time=0;s.SavedCamera.Valid=false;s.AppliedRevision=UINT64_MAX;return true;
}
bool jEditor::SetPlayTime(float seconds)
{
    auto& s=*Runtime->Data;
    if(!s.Owner()||!s.Doc.IsPlaying()||!std::isfinite(seconds)||seconds<0||seconds>86400)return false;
    s.Time=seconds;s.AppliedRevision=UINT64_MAX;return true;
}
void jEditor::Tick(float deltaTime,jCamera* camera)
{
    auto& s=*Runtime->Data;if(!s.Owner()||s.ShuttingDown)return;s.Camera=camera;
    const auto mappingEpoch = jEditorTools::PollMaterialMappingTexturePreparation();
    if (mappingEpoch != s.MappingTextureEpoch)
    {
        s.MappingTextureEpoch = mappingEpoch;
        for (auto& [path, graph] : s.GraphDefaults) if (graph.Pending)
        {
            graph.Dirty = true;
            for (auto& [id, binding] : s.Bindings) if (binding.Last.Material.GraphAsset == path) binding.MaterialApplied = false;
            s.AppliedRevision = UINT64_MAX;
        }
    }
    if(g_rhi)jModelUploadSession::CollectRetired();
    if(s.Doc.IsPlaying()&&std::isfinite(deltaTime)&&deltaTime>0)s.Time+=Min(deltaTime,0.1f);
    if(s.Doc.Revision()!=s.AppliedRevision||!s.Imports.empty()||!s.Uploads.empty()||s.Doc.IsPlaying())SyncDocumentToWorld();
    else {
        // A legacy callback may have used TRS setters during Game.Update and
        // cleared the authored override. Reassert only those bindings; static
        // scenes keep the revision fast path and do not rebuild hierarchy maps.
        if(s.Enabled)for(auto& [id,b]:s.Bindings)
            if(b.HasAppliedWorld&&((b.Object&&b.Object->IsPostUpdate&&b.Object->PostUpdateFunc)
                ||(b.Light&&b.Light->PreUpdateLambda)))s.Apply(b,b.Last,b.AppliedWorld);
        s.CollectRetired();
    }
    if(s.Doc.SelectedId()&&(s.Selected.empty()||std::find(s.Selected.begin(),s.Selected.end(),s.Doc.SelectedId())==s.Selected.end()))s.Selected={s.Doc.SelectedId()};
    std::erase_if(s.Selected,[&](auto id){return !s.Doc.Find(id);});
    if(!s.Doc.SelectedId())s.Selected.clear();
    if(s.UpdateProcedural)for(auto& [id,b]:s.Bindings)if(b.Object&&(b.Last.Kind==EntityKind::ParticleEmitter||b.Last.Kind==EntityKind::Terrain||b.Last.Kind==EntityKind::Water))s.UpdateProcedural(b.Last,*b.Object,deltaTime,s.Time);
    if(s.Doc.IsPlaying()&&camera){
        std::map<EntityId,Matrix> cache;
        for(auto& [id,b]:s.Bindings)if(b.Camera&&b.Last.Camera.Primary&&s.Doc.IsEffectivelyVisible(id)){
            const Matrix world=s.World(id,cache);const Vector pos(world.m30,world.m31,world.m32);const Vector forward=world.TransformDirection(Vector(0,0,1)).GetNormalize();const Vector up=world.TransformDirection(Vector::UpVector).GetNormalize();
            const auto& p=b.Last.Camera;jCamera::SetCamera(camera,pos,pos+forward,pos+up,DegreeToRadian(p.VerticalFovDegrees),p.NearPlane,p.FarPlane,camera->Width,camera->Height,true,1);camera->UpdateCamera();break;
        }
    }
}
void jEditor::SetViewportInputState(bool hovered,bool focused){auto& s=*Runtime->Data;if(!s.Owner())return;s.Hovered=hovered;s.Focused=focused;}
void jEditor::SetShortcutInputState(bool focused){auto& s=*Runtime->Data;if(s.Owner())s.ShortcutFocused=focused;}

void jEditor::ProcessInput(float deltaTime,jCamera* camera)
{
    auto& s=*Runtime->Data;if(!s.Owner())return;
    std::array<bool,static_cast<size_t>(EInputKey::MAX)> pressed{};
    for(size_t i=0;i<s.Keys.size();++i){const bool down=IsKeyDown(static_cast<EInputKey>(i));pressed[i]=down&&!s.Keys[i];s.Keys[i]=down;}
    if(!s.Enabled||!ImGui::GetCurrentContext())return;
    const auto& io=ImGui::GetIO();
    if(io.WantTextInput||ImGui::IsAnyItemActive()||ImGuizmo::IsUsing()
        ||(io.WantCaptureKeyboard&&!s.Focused&&!s.ShortcutFocused)||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup))return;
    const auto hit=[&](EInputKey key){return pressed[InputKeyIndex(key)];};
    const bool ctrl=IsKeyDown(EInputKey::CTRL),shift=IsKeyDown(EInputKey::SHIFT),alt=IsKeyDown(EInputKey::ALT);
    if(ctrl){
        if(hit(EInputKey::Z)){if(shift)s.Doc.Redo();else s.Doc.Undo();}
        if(hit(EInputKey::Y))s.Doc.Redo();
        if(hit(EInputKey::D))DuplicateSelected();
        if(hit(EInputKey::G))Placement.UseSnap=!Placement.UseSnap;
        return;
    }
    if(alt)return;
    if(shift){if(hit(EInputKey::W))Placement.GizmoOperation=ImGuizmo::TRANSLATE;if(hit(EInputKey::E))Placement.GizmoOperation=ImGuizmo::ROTATE;if(hit(EInputKey::R))Placement.GizmoOperation=ImGuizmo::SCALE;return;}
    if(hit(EInputKey::F))FocusSelected(camera);
    if(hit(EInputKey::DELETE_KEY))DeleteSelected();
    if(hit(EInputKey::ESCAPE))SelectEntity(0);
    if(hit(EInputKey::Q)&&!g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down)Placement.GizmoMode=Placement.GizmoMode==ImGuizmo::WORLD?ImGuizmo::LOCAL:ImGuizmo::WORLD;
    if(hit(EInputKey::L)){Placement.CurrentTab=EPlacementTab::LIGHT;Placement.SelectedLightType=EPlacementLightType::POINT;}
    if(hit(EInputKey::K)){Placement.CurrentTab=EPlacementTab::LIGHT;Placement.SelectedLightType=EPlacementLightType::SPOT;}
    if(hit(EInputKey::J)){Placement.CurrentTab=EPlacementTab::LIGHT;Placement.SelectedLightType=EPlacementLightType::DIRECTIONAL;}
    if(hit(EInputKey::N)){Placement.CurrentTab=EPlacementTab::SHAPE;Placement.SelectedShapeType=EPlacementShapeType::CUBE;}
    if(hit(EInputKey::M)){Placement.CurrentTab=EPlacementTab::SHAPE;Placement.SelectedShapeType=EPlacementShapeType::SPHERE;}
    if(hit(EInputKey::P))CreateEntity(Placement.CurrentTab==EPlacementTab::SHAPE?(Placement.SelectedShapeType==EPlacementShapeType::SPHERE?EntityKind::Sphere:EntityKind::Cube):(Placement.SelectedLightType==EPlacementLightType::SPOT?EntityKind::SpotLight:Placement.SelectedLightType==EPlacementLightType::DIRECTIONAL?EntityKind::DirectionalLight:EntityKind::PointLight));
    if(hit(EInputKey::UP)||hit(EInputKey::DOWN)){
        std::vector<EntityId> ids;for(auto& [id,e]:s.Doc.Entities())ids.push_back(id);
        if(!ids.empty()){auto it=std::find(ids.begin(),ids.end(),s.Doc.SelectedId());int index=it==ids.end()?-1:static_cast<int>(it-ids.begin());index=std::clamp(index+(hit(EInputKey::DOWN)?1:-1),0,static_cast<int>(ids.size())-1);SelectEntity(ids[index]);}
    }
}
bool jEditor::HandleCameraInput(float deltaTime,jCamera* camera)
{
    auto& s=*Runtime->Data;if(!s.Owner()||!s.Enabled)return false;if(!camera||!ImGui::GetCurrentContext())return true;
    const auto& io=ImGui::GetIO();
    if((!s.Focused&&!s.Hovered)||io.WantTextInput||(io.WantCaptureKeyboard&&!s.Focused)
        ||(ImGui::IsAnyItemActive()&&!s.Hovered)||ImGuizmo::IsUsing()||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup))return true;
    // Editor modifiers own the full chord. Shift+W must never also fly forward.
    if(IsKeyDown(EInputKey::SHIFT)||IsKeyDown(EInputKey::CTRL)||IsKeyDown(EInputKey::ALT))return true;
    if(IsKeyDown(EInputKey::PLUS))s.MoveSpeed=Min(s.MoveSpeed+200*deltaTime,10000.0f);
    if(IsKeyDown(EInputKey::MINUS))s.MoveSpeed=Max(s.MoveSpeed-200*deltaTime,1.0f);
    const float distance=s.MoveSpeed*std::clamp(deltaTime,0.0f,0.1f);
    if(jEditorProjectSettings::ActionDown("camera.forward",EInputKey::W))camera->MoveForward(distance);
    if(jEditorProjectSettings::ActionDown("camera.back",EInputKey::S))camera->MoveForward(-distance);
    if(jEditorProjectSettings::ActionDown("camera.left",EInputKey::A))camera->MoveShift(-distance);
    if(jEditorProjectSettings::ActionDown("camera.right",EInputKey::D))camera->MoveShift(distance);
    if(g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down){const float vertical=(jEditorProjectSettings::ActionDown("camera.up",EInputKey::E)?distance:0)-(jEditorProjectSettings::ActionDown("camera.down",EInputKey::Q)?distance:0);const Vector offset=Vector::UpVector*vertical;camera->Pos+=offset;camera->Target+=offset;camera->Up+=offset;}
    return true;
}
bool jEditor::HandleCameraMouseMove(int32 xOffset,int32 yOffset,jCamera* camera)
{
    auto& s=*Runtime->Data;if(!s.Owner()||!s.Enabled)return false;if(!camera||!s.Hovered||ImGuizmo::IsUsing()||ImGuizmo::IsOver())return true;
    const bool orbit=IsKeyDown(EInputKey::ALT)&&g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down;
    const bool fly=g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down;
    if(!orbit&&!fly)return true;
    Vector angles=camera->GetEulerAngle()+Vector(yOffset*0.0025f,xOffset*0.0025f,0);
    if(orbit){
        float radius=(camera->Pos-s.OrbitTarget).Length();if(!std::isfinite(radius)||radius<1)radius=Max(1.0f,camera->Distance);
        Vector forward,right,up;jCamera::GetForwardRightUpFromEulerAngle(forward,right,up,angles);
        const Vector pos=s.OrbitTarget-forward*radius;jCamera::SetCamera(camera,pos,s.OrbitTarget,pos+up,camera->FOVRad,camera->Near,camera->Far,camera->Width,camera->Height,camera->IsPerspectiveProjection,radius);
    }else camera->SetEulerAngle(angles);
    return true;
}

void jEditor::RenderViewportGizmo(jCamera* camera,float x,float y,float width,float height)
{
    auto& s=*Runtime->Data;if(!s.Owner()||!ImGui::GetCurrentContext())return;
    ImGuizmo::BeginFrame();
    if(s.GizmoTransaction&&!ImGui::IsMouseDown(0)){s.Doc.CommitTransaction();s.GizmoTransaction=false;}
    if(!s.Enabled||!camera||!s.Doc.SelectedId())return;
    const auto roots=s.SelectionRoots();if(roots.empty())return;
    for(auto id:roots)if(s.Doc.IsEffectivelyLocked(id))return;
    std::map<EntityId,Matrix> worlds;Vector center=Vector::ZeroVector;
    for(auto id:roots){const auto m=s.World(id,worlds);center+=Vector(m.m30,m.m31,m.m32);}center/=static_cast<float>(roots.size());
    Matrix pivot=s.World(roots.front(),worlds);pivot.m30=center.x;pivot.m31=center.y;pivot.m32=center.z;
    const Matrix before=pivot;
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());ImGuizmo::SetOrthographic(!camera->IsPerspectiveProjection);
    const auto& io=ImGui::GetIO();ImGuizmo::SetRect(x,y,width>0?width:io.DisplaySize.x,height>0?height:io.DisplaySize.y);
    float* snap=Placement.UseSnap?(Placement.GizmoOperation==ImGuizmo::TRANSLATE?Placement.TranslationSnap:Placement.GizmoOperation==ImGuizmo::ROTATE?&Placement.RotationSnap:Placement.ScaleSnap):nullptr;
    const bool changed=ImGuizmo::Manipulate(camera->View.mm,camera->Projection.mm,Placement.GizmoOperation,Placement.GizmoMode,pivot.mm,nullptr,snap);
    if(changed){
        if(!s.GizmoTransaction)s.GizmoTransaction=s.Doc.BeginTransaction("Transform selection");
        const Matrix delta=pivot*before.GetInverse();
        std::vector<Entity> updates;bool valid=true;
        for(auto id:roots){auto entity=*s.Doc.Find(id);const Matrix desired=delta*s.World(id,worlds);Matrix parent{IdentityType};if(entity.Parent)parent=s.World(entity.Parent,worlds);
            if(!Decompose(parent.GetInverse()*desired,entity.Local)){valid=false;break;}updates.push_back(entity);}
        if(valid){for(auto& entity:updates)s.Doc.Update(entity,"Transform selection");}
        else s.Error="This transform introduces local shear. Use a parent group or uniform scale; scene was left unchanged.";
    }
    if(s.GizmoTransaction&&!ImGuizmo::IsUsing()){s.Doc.CommitTransaction();s.GizmoTransaction=false;}
}
#endif
