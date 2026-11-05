#include "pch.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorProcedural.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Scene/jObject.h"
#include "Scene/jMeshObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jCamera.h"
#include "Material/jMaterial.h"
#include <fstream>

namespace
{
bool MatrixNear(const Matrix& a,const Matrix& b)
{
    for(int i=0;i<16;++i)if(std::abs(a.mm[i]-b.mm[i])>0.001f)return false;
    return true;
}
}
#endif

int RunEditorAuthoringRuntimeTests()
{
#ifdef ENABLE_EDITOR_FEATURES
    using namespace jEditorAuthoring;
    int failures=0;
    const auto expect=[&](bool condition,const char* name){std::cout<<(condition?"PASS ":"FAIL ")<<name<<std::endl;failures+=!condition;};
    auto* previousContext=ImGui::GetCurrentContext();auto* context=ImGui::CreateContext();
    const auto oldKeys=g_KeyState;const auto oldMouse=g_MouseState;auto* oldEditor=g_Editor;
    ResetAllKeyState();ImGui::GetIO().WantCaptureKeyboard=false;ImGui::GetIO().WantTextInput=false;
    const auto directory=std::filesystem::temp_directory_path()/("jengine-authoring-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory/"Assets");
    {std::ofstream source(directory/"Assets/fixture.gltf");source<<"{}";}
    {
        jEditor editor;g_Editor=&editor;editor.SetProjectRoot(directory);
        expect(!editor.IsAuthoringEnabled(),"authoring leaves legacy placement compatibility opt-in");
        const auto defaultEmitter=editor.CreateEntity(EntityKind::ParticleEmitter,"Default emitter");
        expect(defaultEmitter&&editor.GetDocument().Find(defaultEmitter)->Material.OverrideEnabled,
            "new particle emitters enable the actual authored-material publication path by default");
        editor.GetDocument().Delete(defaultEmitter);
        jObject original;auto geometry=std::make_shared<jRenderObjectGeometryData>();auto material=std::make_shared<jMaterial>();
        material->Surface.emplace().Subsurface = .45f;
        material->Surface->SpecularWeight = 1.4f; material->Surface->DiffuseOcclusion = .8f; material->Surface->PreScatterWeight = .65f;
        for(int n=0;n<2;++n){
            auto* mesh=new jRenderObjectElement();mesh->GeometryDataPtr=geometry;mesh->MaterialPtr=material;
            mesh->SubMesh={n*3,n*3+3,n*3,n*3+3,0,"part"};
            mesh->SetPos(Vector(n?15.0f:-10.0f,n?3.0f:2.0f,n?5.0f:0));mesh->SetRot(Vector(0,0,n?0.3f:0.1f));mesh->SetScale(Vector(n?2.0f:1.0f));mesh->UpdateWorldMatrix();original.AddRenderObject(mesh);
        }
        const Matrix firstBase=original.RenderObjects[0]->World,secondBase=original.RenderObjects[1]->World;
        jObject::AddObject(&original);editor.TrackWorldObjectSource(&original,"Assets/fixture.gltf","fixture-asset",1);
        std::unique_ptr<jPointLight> sourceLight(jLight::CreatePointLight(Vector(20,30,40),Vector(2,3,4),500,Vector::OneVector,Vector::OneVector,64));
        jAdvancedShadows::Settings importedShadows;
        importedShadows.Method=jAdvancedShadows::Representation::Deep;
        importedShadows.PointProjection=jAdvancedShadows::PointProjection::DualParaboloid;
        sourceLight->SetShadowOverride(importedShadows);
        jLight::AddLights(sourceLight.get());
        expect(editor.CaptureWorldToDocument(),"capture binds registered model and real light to plain authored entities");
        const auto modelId=editor.EntityForObject(&original);EntityId lightId=0;
        for(const auto& [id,e]:editor.GetDocument().Entities())if(editor.LightForEntity(id)==sourceLight.get())lightId=id;
        expect(modelId&&lightId&&editor.GetDocument().Find(modelId)->SourceAsset=="Assets/fixture.gltf"
            &&editor.GetDocument().Find(modelId)->MeshLocalTransforms.size()==2,"capture persists source references and every mesh-local transform");
        expect(lightId&&editor.GetDocument().Find(lightId)->Light.ShadowOverride==sourceLight->GetShadowOverride(),
            "world capture persists the actual source light shadow override without inventing a global override");
        auto camera=std::unique_ptr<jCamera>(jCamera::CreateCamera(Vector(0,0,-100),Vector::ZeroVector,Vector(0,1,-100),DegreeToRadian(60),0.1f,10000,1280,720,true));
        camera->UpdateCamera();editor.Tick(0,camera.get());
        {
            auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,720);io.DeltaTime=1.0f/60;
            unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
            editor.SetAuthoringEnabled(true);editor.SelectEntity(modelId);
            const auto beforeDraw=editor.GetDocument().ToJson();
            for(int frame=0;frame<2;++frame){
                ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1280,720));
                ImGui::Begin("Actual authoring gizmo smoke");
                editor.RenderViewportGizmo(camera.get(),0,0,1280,720);ImGui::End();ImGui::Render();
            }
            expect(editor.GetDocument().ToJson()==beforeDraw&&ImGui::GetDrawData()->TotalVtxCount>0,
                "actual authoring gizmo draws without input or an invalid initial operation and preserves authored state");
        }
        auto& document=editor.GetDocument();auto model=*document.Find(modelId);
        expect(!model.ShadowCaster,"captured models inherit their source shadow-casting flag by default");
        model.ShadowCaster=false;document.Update(model,"Receiver only");editor.Tick(0,camera.get());jObject::FlushDirtyState();
        expect(original.SkipShadowMapGen&&std::find(jObject::GetStaticObject().begin(),jObject::GetStaticObject().end(),&original)!=jObject::GetStaticObject().end()
            &&std::find(jObject::GetShadowCasterObject().begin(),jObject::GetShadowCasterObject().end(),&original)==jObject::GetShadowCasterObject().end()
            &&std::find(jObject::GetShadowCasterRenderObject().begin(),jObject::GetShadowCasterRenderObject().end(),original.RenderObjects[0])==jObject::GetShadowCasterRenderObject().end(),
            "owner publication removes actual object and render meshes from casters while keeping the receiver registered");
        editor.SyncDocumentToWorld();
        expect(!original.DirtyObjectState,"unchanged caster value avoids redundant dirty-state publication on a full document sync");
        editor.SelectEntity(modelId);const auto casterCopyId=editor.DuplicateSelected();editor.Tick(0,camera.get());
        auto* casterCopy=editor.ObjectForEntity(casterCopyId);
        const bool copyReceiver=casterCopy&&casterCopy->SkipShadowMapGen;
        if(const auto* copied=document.Find(casterCopyId)){
            auto casterCopyEntity=*copied;casterCopyEntity.ShadowCaster.reset();
            document.Update(casterCopyEntity,"Inherit duplicate caster flag");editor.Tick(0,camera.get());jObject::FlushDirtyState();
        }
        expect(copyReceiver&&casterCopy&&!casterCopy->SkipShadowMapGen&&original.SkipShadowMapGen,
            "duplicate retains the actual prototype baseline so clearing its override does not inherit the edited source instance flag");
        document.Delete(casterCopyId);editor.Tick(0,camera.get());
        model=*document.Find(modelId);model.ShadowCaster.reset();document.Update(model,"Inherit source caster flag");
        editor.Tick(0,camera.get());jObject::FlushDirtyState();editor.SelectEntity(modelId);
        expect(!original.SkipShadowMapGen&&std::count(jObject::GetShadowCasterObject().begin(),jObject::GetShadowCasterObject().end(),&original)==1,
            "clearing object casting override restores exactly one inherited source caster registration");
        model=*document.Find(modelId);
        model.Local.Position={100,20,-5};model.Local.RotationDegrees={0,90,0};model.Local.Scale={2,2,2};
        expect(document.Update(model,"Move model"),"authored transform enters the real command history");editor.Tick(0,camera.get());
        float matrix[16];document.GetWorldMatrix(modelId,matrix);const Matrix modelWorld(matrix);
        expect(MatrixNear(original.RenderObjects[0]->World,modelWorld*firstBase)
            &&MatrixNear(original.RenderObjects[1]->World,modelWorld*secondBase),"runtime whole-model transform preserves mesh offsets rotations and scales");
        const auto group=document.Create(EntityKind::Empty,"Parent");
        expect(document.Reparent(modelId,group),"reparenting accepts a stable entity ID without a runtime pointer");
        auto parent=*document.Find(group);parent.Local.RotationDegrees={0,0,35};parent.Local.Scale={2,1,3};parent.Local.Position={7,8,9};
        document.Update(parent,"Parent transform");editor.Tick(0,camera.get());document.GetWorldMatrix(modelId,matrix);
        expect(MatrixNear(original.RenderObjects[0]->World,Matrix(matrix)*firstBase),"parent affine transform preserves exact inherited matrix including shear");
        const auto exact=original.RenderObjects[0]->World;original.Update(0);
        expect(MatrixNear(original.RenderObjects[0]->World,exact),"normal runtime object update cannot overwrite authored affine matrices");
        editor.SelectEntity(modelId);parent=*document.Find(group);parent.Visible=false;document.Update(parent,"Hide subtree");editor.Tick(0,camera.get());
        expect(std::find(jObject::GetStaticObject().begin(),jObject::GetStaticObject().end(),&original)==jObject::GetStaticObject().end()
            &&document.SelectedId()==modelId,"inherited visibility removes real render/pick entries while stable selection survives");
        parent.Visible=true;document.Update(parent,"Show subtree");editor.Tick(0,camera.get());
        expect(std::count(jObject::GetStaticObject().begin(),jObject::GetStaticObject().end(),&original)==1,"show restores exactly one scene registration");
        editor.SelectEntity(modelId);const auto copyId=editor.DuplicateSelected();editor.Tick(0,camera.get());auto* copy=editor.ObjectForEntity(copyId);
        expect(copyId&&copy&&copy!=&original&&copy->RenderObjects.size()==2,"duplicate creates independent runtime object and render-object identities");
        expect(copy&&copy->RenderObjects[0]->GeometryDataPtr==geometry&&copy->RenderObjects[0]->MaterialPtr==material
            &&dynamic_cast<jRenderObjectElement*>(copy->RenderObjects[0])->SubMesh.StartVertex==0,
            "duplicate shares geometry/material handles and retains submesh draw ranges without reimport");
        expect(copy&&MatrixNear(copy->RenderObjects[1]->World,original.RenderObjects[1]->World),"duplicate preserves per-mesh world transforms and parent pivot");
        editor.SelectEntity(modelId);expect(editor.DeleteSelected(),"delete records a document command instead of destroying borrowed source ownership");editor.Tick(0,camera.get());
        expect(!document.Find(modelId)&&jObject::FindObjectByID(original.GetObjectID())==&original,"deleting borrowed model preserves the original scene owner's allocation");
        expect(document.Undo(),"delete can be undone");editor.Tick(0,camera.get());auto* restored=editor.ObjectForEntity(modelId);
        expect(restored&&restored!=&original&&editor.EntityForObject(restored)==modelId
            &&restored->RenderObjects[0]->GeometryDataPtr==geometry,"undo reconstructs a live instance with the same authored ID and shared geometry");
        const auto savedWorld=restored->RenderObjects[0]->World;
        const auto validMeshEntity=*document.Find(modelId);
        auto mismatched=validMeshEntity;mismatched.MeshLocalTransforms.resize(1);mismatched.Local.Position.X+=123;
        document.Update(mismatched,"Invalid source topology");
        expect(!editor.SyncDocumentToWorld()&&editor.LastError().find("count")!=std::string::npos
            &&MatrixNear(restored->RenderObjects[0]->World,savedWorld),
            "mismatched saved mesh count rejects publication and preserves the last real live transform");
        document.Update(validMeshEntity,"Restore mesh offsets");editor.Tick(0,camera.get());
        auto customMeshEntity=validMeshEntity;customMeshEntity.MeshLocalTransforms[0].Position.X+=25;
        document.Update(customMeshEntity,"Edit mesh offset");editor.Tick(0,camera.get());
        const bool customOffsetApplied=!MatrixNear(restored->RenderObjects[0]->World,savedWorld);
        customMeshEntity.MeshLocalTransforms.clear();document.Update(customMeshEntity,"Use source mesh offsets");editor.Tick(0,camera.get());
        expect(customOffsetApplied&&MatrixNear(restored->RenderObjects[0]->World,savedWorld),
            "clearing saved mesh-local overrides restores actual source-default offsets");
        document.Update(validMeshEntity,"Restore persisted mesh offsets");editor.Tick(0,camera.get());
        expect(document.BeginTransaction("Drag preview"),"runtime transform preview starts one history transaction");
        model=*document.Find(modelId);model.Local.Position.X+=50;document.Update(model);editor.Tick(0,camera.get());
        document.CancelTransaction();editor.Tick(0,camera.get());
        expect(MatrixNear(restored->RenderObjects[0]->World,savedWorld),"cancelling transform preview restores the actual world matrix");
        const auto scenePath=directory/"Scenes/runtime.jscene";
        const auto beforeFailedSave=document.Revision();
        const bool missingDirectoryRejected=!document.Save(scenePath);
        std::cout<<"DETAIL missing scene directory: "<<document.LastError()<<std::endl;
        expect(missingDirectoryRejected&&document.LastError()=="Scene parent directory does not exist"&&document.Revision()==beforeFailedSave,
            "scene save reports missing parent without changing the live document");
        std::filesystem::create_directories(scenePath.parent_path());
        const bool saved=document.Save(scenePath);if(!saved)std::cout<<"DETAIL scene save: "<<document.LastError()<<std::endl;
        expect(saved,"live scene snapshot saves through real versioned scene persistence");
        model=*document.Find(modelId);model.Local.Position.X+=100;document.Update(model);editor.Tick(0,camera.get());
        const bool loaded=document.Load(scenePath);if(!loaded)std::cout<<"DETAIL scene load: "<<document.LastError()<<std::endl;
        expect(loaded,"saved document reload succeeds");editor.Tick(0,camera.get());restored=editor.ObjectForEntity(modelId);
        expect(restored&&MatrixNear(restored->RenderObjects[0]->World,savedWorld)
            &&editor.LightForEntity(lightId),"save-load restores live model mesh transforms and light bindings from serialized data");
        editor.SelectEntity(modelId);expect(editor.FocusSelected(camera.get())&&camera->Distance>0,"focus frames actual selected geometry with finite camera distance");
        const auto cameraBefore=camera->Pos;
        expect(!editor.SetPlayTime(1),"timeline seek rejects edit mode");
        model=*document.Find(modelId);model.Animation.Enabled=true;model.Animation.Loop=false;model.Animation.Duration=2;
        auto animationEnd=model.Local;animationEnd.Position.X+=40;
        model.Animation.Keys={{0,model.Local},{2,animationEnd}};document.Update(model,"Animate model");editor.Tick(0,camera.get());
        expect(editor.BeginPlay(),"play begins with an edit-state snapshot");
        const auto previewRevision=document.Revision();const auto previewDirty=document.IsDirty();const auto previewUndo=document.UndoLabel();
        const bool sought=editor.SetPlayTime(1);editor.Tick(0,camera.get());
        document.GetWorldMatrix(group,matrix);const auto animationDelta=Matrix(matrix).TransformDirection(Vector(20,0,0));
        auto previewWorld=savedWorld;previewWorld.m30+=animationDelta.x;previewWorld.m31+=animationDelta.y;previewWorld.m32+=animationDelta.z;
        expect(sought&&MatrixNear(editor.ObjectForEntity(modelId)->RenderObjects[0]->World,previewWorld)
            &&document.Revision()==previewRevision&&document.IsDirty()==previewDirty&&document.UndoLabel()==previewUndo,
            "timeline seek changes the real animated world without editing document history or dirty state");
        expect(!editor.SetPlayTime(-1)&&!editor.SetPlayTime(86401)&&!editor.SetPlayTime(std::numeric_limits<float>::quiet_NaN())
            &&!editor.SetPlayTime(std::numeric_limits<float>::infinity())&&editor.PlayTime()==1
            &&editor.SetPlayTime(86400)&&editor.PlayTime()==86400&&editor.SetPlayTime(0),
            "timeline seek validates finite inclusive zero-to-one-day bounds atomically");
        model=*document.Find(modelId);model.Local.Position.Z+=200;document.Update(model);const auto temporary=editor.CreateEntity(EntityKind::PointLight,"Play only");
        editor.Tick(0.05f,camera.get());expect(editor.LightForEntity(temporary)&&editor.PlayTime()>0,"play state creates actual temporary runtime entities");
        expect(editor.StopPlay(),"stop restores the edit document");editor.Tick(0,camera.get());
        expect(!document.Find(temporary)&&!editor.LightForEntity(temporary)&&camera->Pos.IsNearlyEqual(cameraBefore)
            &&MatrixNear(editor.ObjectForEntity(modelId)->RenderObjects[0]->World,savedWorld),"stop removes play-only objects and restores edited transforms and camera");
        editor.SetAuthoringEnabled(true);editor.SetViewportInputState(true,true);editor.SetShortcutInputState(true);
        ResetAllKeyState();SetKeyDownState(EInputKey::SHIFT,true);SetKeyDownState(EInputKey::W,true);const auto beforeShift=camera->Pos;
        expect(editor.HandleCameraInput(0.05f,camera.get())&&camera->Pos.IsNearlyEqual(beforeShift),"Shift+W is consumed by editor tools and cannot also move the camera");
        SetKeyDownState(EInputKey::SHIFT,false);editor.HandleCameraInput(0.05f,camera.get());
        expect(!camera->Pos.IsNearlyEqual(beforeShift),"focused viewport WASD uses the real camera movement path");
        const auto beforeTyping=camera->Pos;ImGui::GetIO().WantTextInput=true;editor.HandleCameraInput(0.05f,camera.get());
        expect(camera->Pos.IsNearlyEqual(beforeTyping),"typing in UI prevents authoring camera movement");ImGui::GetIO().WantTextInput=false;
        ResetAllKeyState();const auto angles=camera->GetEulerAngle();g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down=true;
        editor.HandleCameraMouseMove(25,10,camera.get());expect(!camera->GetEulerAngle().IsNearlyEqual(angles),"right-drag fly look changes the actual camera orientation");
        g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down=false;editor.FocusSelected(camera.get());g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down=true;
        SetKeyDownState(EInputKey::ALT,true);const auto oldDistance=(camera->Pos-camera->Target).Length();editor.HandleCameraMouseMove(20,5,camera.get());
        expect(std::abs((camera->Pos-camera->Target).Length()-oldDistance)<0.01f,"Alt-left orbit preserves the focus radius");
        ResetAllKeyState();g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down=false;
        int lightCallbacks=0;auto* authoredLight=dynamic_cast<jPointLight*>(editor.LightForEntity(lightId));
        auto shadowEntity=*document.Find(lightId);
        auto changedShadows=importedShadows;changedShadows.Method=jAdvancedShadows::Representation::Variance;
        shadowEntity.Light.ShadowOverride=changedShadows;
        const bool shadowUpdated=document.Update(shadowEntity,"Change actual light shadows");editor.Tick(0,camera.get());
        expect(shadowUpdated&&authoredLight->GetShadowOverride()==shadowEntity.Light.ShadowOverride,
            "owner runtime applies a persisted shadow override to the actual reconstructed light");
        const bool shadowUndone=document.Undo();editor.Tick(0,camera.get());
        expect(shadowUndone&&authoredLight->GetShadowOverride()==std::optional<jAdvancedShadows::Settings>(importedShadows),
            "Undo republishes the previous actual per-light shadow method");
        shadowEntity=*document.Find(lightId);shadowEntity.Light.ShadowOverride.reset();
        const bool shadowCleared=document.Update(shadowEntity,"Inherit shadows");editor.Tick(0,camera.get());
        expect(shadowCleared&&!authoredLight->GetShadowOverride(),"clearing an override returns the runtime light to project/global inheritance");
        document.Undo();editor.Tick(0,camera.get());
        auto lightEntity=*document.Find(lightId);lightEntity.Local.Position.X+=17;lightEntity.Light.Range+=45;document.Update(lightEntity,"Move authored light");
        authoredLight->PreUpdateLambda=[&](jLight* light,float){++lightCallbacks;light->SetPosition(Vector(999));};
        editor.Tick(0,camera.get());const auto lightPosition=Vector(lightEntity.Local.Position.X,lightEntity.Local.Position.Y,lightEntity.Local.Position.Z);
        const auto showLight=[&](const char* stage){const auto p=authoredLight->GetPosition();const auto c=authoredLight->GetLightCamra(0)->Pos;
            std::cout<<"DETAIL "<<stage<<" callbacks="<<lightCallbacks<<" expected="<<lightPosition.x<<","<<lightPosition.y<<","<<lightPosition.z
                <<" actual="<<p.x<<","<<p.y<<","<<p.z<<" shadow="<<c.x<<","<<c.y<<","<<c.z
                <<" volume="<<authoredLight->GetLightWorldMatrix()->m30<<" error="<<editor.LastError()<<std::endl;};
        showLight("new authored light");
        expect(lightCallbacks==0&&authoredLight->GetPosition().IsNearlyEqual(lightPosition)
            &&authoredLight->GetLightCamra(0)->Pos.IsNearlyEqual(lightPosition)
            &&std::abs(authoredLight->GetLightWorldMatrix()->m30-lightPosition.x)<0.001f,
            "authored light publication refreshes real shadow cameras and volume before rendering without rerunning its callback");
        authoredLight->Update(0.1f);editor.Tick(0,camera.get());
        showLight("scripted authored light");
        expect(lightCallbacks==1&&authoredLight->PreUpdateLambda&&authoredLight->GetPosition().IsNearlyEqual(lightPosition)
            &&authoredLight->GetLightCamra(0)->Pos.IsNearlyEqual(lightPosition),
            "revision-stable authoring reasserts scripted lights and preserves callback ownership");
        authoredLight->PreUpdateLambda={};
        const auto spotId=editor.CreateEntity(EntityKind::SpotLight,"Authored spot");
        auto spotEntity=*document.Find(spotId);spotEntity.Local.Position={70,80,90};spotEntity.Local.RotationDegrees={15,30,10};
        document.Update(spotEntity,"Position spot");editor.Tick(0,camera.get());
        auto* authoredSpot=dynamic_cast<jSpotLight*>(editor.LightForEntity(spotId));
        const bool initialSpotPosition=authoredSpot&&authoredSpot->GetLightCamra(0)->Pos.IsNearlyEqual(Vector(70,80,90));
        const auto spotView=authoredSpot?authoredSpot->GetLightCamra(0)->View:Matrix(IdentityType);
        spotEntity.Local.Position={110,100,85};document.Update(spotEntity,"Move spot");editor.Tick(0,camera.get());
        expect(initialSpotPosition&&authoredSpot&&authoredSpot->GetLightCamra(0)->Pos.IsNearlyEqual(Vector(110,100,85))
            &&!MatrixNear(authoredSpot->GetLightCamra(0)->View,spotView)
            &&std::abs(authoredSpot->GetLightWorldMatrix()->m30-110)<0.001f,
            "new and moved authored spotlights publish shadow-camera position view and light volume in the same frame");
        document.Delete(spotId);editor.Tick(0,camera.get());editor.SelectEntity(modelId);
        restored=editor.ObjectForEntity(modelId);restored->PostUpdateFunc=[](jObject* object,float){object->RenderObjects[0]->SetPos(Vector(999));};
        restored->Update(0.1f);editor.Tick(0,camera.get());
        expect(MatrixNear(restored->RenderObjects[0]->World,savedWorld),"revision-stable authoring reasserts only legacy-scripted object transforms");
        restored->PostUpdateFunc={};
        const auto pendingModel=editor.AddModel("Assets/fixture.gltf","cancel-asset",1);
        editor.SyncDocumentToWorld();
        expect(editor.PendingImportCount()>0,"model reference queues a real CPU import without publishing a placeholder object");
        editor.CancelPendingImports();
        expect(editor.PendingImportCount()==0&&editor.ObjectForEntity(modelId),"cancelling pending imports retains existing live instances");
        document.Delete(pendingModel);editor.Tick(0,camera.get());
        int materialApplications=0;
        editor.SetMaterialApplier([&](const MaterialProperties&,jObject&){++materialApplications;});
        model=*document.Find(modelId);model.Material.OverrideEnabled=true;model.Material.BaseColorTexture="Assets/base.png";
        model.Material.NormalTexture="Assets/normal.png";document.Update(model,"Texture references");editor.Tick(0,camera.get());
        const int beforeTextureReimport=materialApplications;const auto beforeTextureRevision=document.Revision();
        editor.ReimportAsset("Assets/base.png");editor.Tick(0,camera.get());
        const bool baseRefreshed=materialApplications==beforeTextureReimport+1;
        editor.ReimportAsset("Assets/normal.png");editor.Tick(0,camera.get());
        expect(baseRefreshed&&materialApplications==beforeTextureReimport+2&&document.Revision()==beforeTextureRevision,
            "texture reimport reapplies live base-color and normal material bindings without editing the document");
        auto beforeSSS = editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        expect(beforeSSS->Surface&&beforeSSS->Surface->Subsurface==.45f,
            "older instance data without authored SSS retains its imported canonical subsurface scalar when other properties reapply");
        auto& legacySlot=editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        legacySlot=std::make_shared<jMaterial>(*legacySlot);legacySlot->Surface.reset();legacySlot->SubsurfaceOnlySurface=false;
        model=*document.Find(modelId);model.Material.SubsurfaceOverrideEnabled=true;
        model.Material.SubsurfaceWeight=.7f;model.Material.SubsurfaceColor={.8f,.4f,.2f};
        model.Material.SubsurfaceThicknessWorld=.5f;model.Material.SubsurfaceProfile=7;
        const bool sssUpdated=document.Update(model,"Author real subsurface material");editor.Tick(0,camera.get());
        const auto authoredSSS=editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        expect(sssUpdated&&authoredSSS->Surface&&authoredSSS->Surface->Subsurface==.7f
            &&authoredSSS->Surface->SubsurfaceColor==std::array<float,3>{.8f,.4f,.2f}
            &&authoredSSS->Surface->SubsurfaceThicknessWorld==.5f&&authoredSSS->Surface->SubsurfaceProfile==7&&authoredSSS->SubsurfaceOnlySurface
            &&!editor.ObjectForEntity(modelId)->RenderObjects[1]->MaterialPtr->SubsurfaceOnlySurface
            &&material->Surface->Subsurface==.45f,
            "actual owner COW publication writes canonical SSS while preserving legacy versus explicit surface coverage and the shared original");
        model=*document.Find(modelId);model.Material.SubsurfaceWeight=0;document.Update(model,"Disable material diffusion");editor.Tick(0,camera.get());
        expect(editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr->Surface->Subsurface==0,
            "explicit authored zero weight reaches the canonical material instead of being mistaken for missing legacy data");
        model=*document.Find(modelId);model.Material.SkinOverrideEnabled=true;
        model.Material.ShadingModel=jSurfaceShadingModel::SkinKelemen;model.Material.SubsurfaceSpace=jSubsurfaceSpace::Texture;
        model.Material.SpecularWeight=3;model.Material.DiffuseOcclusion=.5f;model.Material.PreScatterWeight=.25f;
        const bool skinUpdated=document.Update(model,"Author skin model and weights");editor.Tick(0,camera.get());
        const auto authoredSkin=editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        expect(skinUpdated&&authoredSkin->Surface&&authoredSkin->Surface->ShadingModel==jSurfaceShadingModel::SkinKelemen
            &&authoredSkin->Surface->SubsurfaceSpace==jSubsurfaceSpace::Texture&&authoredSkin->Surface->SpecularWeight==3
            &&authoredSkin->Surface->DiffuseOcclusion==.5f&&authoredSkin->Surface->PreScatterWeight==.25f
            &&material->Surface->ShadingModel==jSurfaceShadingModel::StandardPBR&&material->Surface->SpecularWeight==1.4f,
            "actual owner publication writes canonical skin values to a COW instance while preserving the imported source");
        model=*document.Find(modelId);model.Material.SkinOverrideEnabled=false;document.Update(model,"Inherit skin source");editor.Tick(0,camera.get());
        const auto inheritedSkin=editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        expect(inheritedSkin->Surface&&inheritedSkin->Surface->ShadingModel==jSurfaceShadingModel::StandardPBR
            &&inheritedSkin->Surface->SpecularWeight==1.4f&&inheritedSkin->Surface->DiffuseOcclusion==.8f&&inheritedSkin->Surface->PreScatterWeight==.65f,
            "clearing skin overrides restores each actual source material rather than retaining stale authored values");
        const bool skinUndo=document.Undo();editor.Tick(0,camera.get());
        expect(skinUndo&&editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr->Surface->SpecularWeight==3,
            "Undo of skin inheritance republishes authored canonical values through the real owner adapter");
        jEditorTools::MaterialGraph mapping;
        mapping.Nodes = {{1,0,0,jEditorTools::NodeKind::Color,{.2f,.4f,.6f}}};
        jEditorTools::MaterialNode channel;channel.Id=2;channel.Kind=jEditorTools::NodeKind::Channel;channel.InputA=1;channel.Parameters={1,0,0,0};
        mapping.Nodes.push_back(channel);mapping.BaseColor=2;
        std::string mappingError;const std::string mappingPath="Assets/runtime-mapping.jmaterial";
        const bool mappingSaved=jEditorTools::SaveMaterial(mapping,directory/mappingPath,mappingError);
        editor.SetRasterSurfacePublicationEnabled(false);
        const auto guardedWater=editor.CreateEntity(EntityKind::Water,"Ray mode authored water");
        editor.Tick(0,camera.get());
        expect(guardedWater&&!editor.ObjectForEntity(guardedWater)&&editor.LastError().find("Water publication requires")!=std::string::npos,
            "ray mode rejects Water before invoking or allocating its procedural runtime factory");
        document.Delete(guardedWater);
        const auto beforeMappingGate=editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        model=*document.Find(modelId);model.Material.GraphAsset=mappingPath;model.Material.GraphOverrides.emplace();
        model.Material.GraphOverrides->BaseColor=true;model.Material.BaseColor={.7f,.2f,.1f};
        document.Update(model,"Bind compiled graph");editor.Tick(0,camera.get());
        expect(editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr==beforeMappingGate
            &&editor.LastError().find("Mapping material publication requires")!=std::string::npos,
            "ray mode retains the actual material pointer when a raster-only graph is assigned");
        editor.SetRasterSurfacePublicationEnabled(true);editor.Tick(0,camera.get());
        const auto mappedMaterial=editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        expect(mappingSaved&&mappedMaterial->MappingProgram&&mappedMaterial->MappingProgram->Active
            &&mappedMaterial->MappingValues[0].x==.2f
            &&(mappedMaterial->MappingOutputOverridesMask&jMaterialMapping::OverrideBaseColor)!=0&&!material->MappingProgram,
            "real owner runtime binding publishes compiled graph values and local output masks without modifying its shared source");
        const auto mappingRevision=document.Revision();const auto mappingHash=mappedMaterial->MappingProgram?mappedMaterial->MappingProgram->CodeHash:0;
        mapping.Nodes[0].Color.X=.3f;
        const bool changedMapping=jEditorTools::SaveMaterial(mapping,directory/mappingPath,mappingError);
        editor.ReimportAsset(mappingPath);editor.Tick(0,camera.get());
        const auto reimportedMaterial=editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr;
        expect(changedMapping&&reimportedMaterial->MappingProgram&&reimportedMaterial->MappingProgram->CodeHash==mappingHash
            &&reimportedMaterial->MappingValues[0].x==.3f&&document.Revision()==mappingRevision,
            "real graph reimport refreshes bound uniform values while reusing shader code and preserving document history");
        {std::ofstream bad(directory/mappingPath);bad<<"{}";}
        editor.ReimportAsset(mappingPath);editor.Tick(0,camera.get());
        expect(editor.ObjectForEntity(modelId)->RenderObjects[0]->MaterialPtr==reimportedMaterial
            &&!editor.LastError().empty()&&document.Revision()==mappingRevision,
            "invalid mapped graph reimport keeps the actual last-good runtime material pointer and reports a diagnostic");
        model=*document.Find(modelId);model.Locked=true;document.Update(model,"Lock");editor.SelectEntity(modelId);
        expect(!editor.DuplicateSelected()&&!editor.DeleteSelected(),"locked entity blocks runtime duplicate and deletion commands");
        bool foreignAccepted=true;std::thread foreign([&]{foreignAccepted=editor.SyncDocumentToWorld();});foreign.join();
        expect(!foreignAccepted,"runtime publication rejects a non-owner thread before scene mutation");
        editor.ShutdownWorld();editor.ShutdownWorld();
        expect(std::count(jObject::GetStaticObject().begin(),jObject::GetStaticObject().end(),&original)==1
            &&std::count(jLight::GetLights().begin(),jLight::GetLights().end(),sourceLight.get())==1,"idempotent shutdown restores borrowed scene owners and unregisters owned instances");
        jObject::RemoveObject(&original);jLight::RemoveLights(sourceLight.get());
        g_Editor=oldEditor;
    }
    {
        // The real production adapter clones its source and enables the legacy
        // alpha permutation. Runtime must publish explicit graph coverage after
        // that clone, rather than fixing only a native test fixture's flags.
        jEditor editor;g_Editor=&editor;editor.SetProjectRoot(directory);jEditorProcedural::Install(editor);
        jObject source;
        auto legacy=std::make_shared<jMaterial>();legacy->bRaytracingAlphaTest=true;legacy->RaytracingAlphaCutoff=.3f;
        auto masked=std::make_shared<jMaterial>();auto& sourceSurface=masked->Surface.emplace();
        sourceSurface.AlphaMode=jSurfaceAlphaMode::Mask;sourceSurface.AlphaCutoff=.2f;sourceSurface.DoubleSided=true;sourceSurface.LegacyPackedMap=true;
        sourceSurface.Transmission=.4f;sourceSurface.Ior=1.6f;masked->bRaytracingAlphaTest=true;masked->RaytracingAlphaCutoff=.2f;
        for(const auto& material:{legacy,masked}){auto* mesh=new jRenderObject();mesh->MaterialPtr=material;source.AddRenderObject(mesh);}
        jObject::AddObject(&source);editor.TrackWorldObjectSource(&source,"Assets/fixture.gltf","coverage-source",1);
        const bool captured=editor.CaptureWorldToDocument();const auto id=editor.EntityForObject(&source);auto& document=editor.GetDocument();
        jEditorTools::MaterialGraph graph;graph.Nodes={{1,0,0,jEditorTools::NodeKind::Color,{.8f,.6f,.4f}}};graph.BaseColor=1;
        auto& opaque=graph.Surface.emplace().Shading;opaque.AlphaMode=jSurfaceAlphaMode::Opaque;opaque.AlphaCutoff=.4f;
        opaque.Transmission=.9f;opaque.Subsurface=.9f;opaque.ShadingModel=jSurfaceShadingModel::SkinKelemen;
        const std::string opaquePath="Assets/coverage-opaque.jmaterial",maskPath="Assets/coverage-mask.jmaterial";std::string error;
        const bool opaqueSaved=jEditorTools::SaveMaterial(graph,directory/opaquePath,error);
        auto entity=id?*document.Find(id):Entity{};entity.Material.OverrideEnabled=true;entity.Material.GraphAsset=opaquePath;
        auto& flags=entity.Material.GraphOverrides.emplace();flags.SubsurfaceWeight=true;flags.SpecularWeight=true;
        entity.Material.SubsurfaceOverrideEnabled=true;entity.Material.SubsurfaceWeight=.35f;entity.Material.SkinOverrideEnabled=true;entity.Material.SpecularWeight=3;
        const bool assigned=document.Update(entity,"Assign explicit opaque graph");editor.Tick(0,nullptr);
        const auto first=source.RenderObjects[0]->MaterialPtr,second=source.RenderObjects[1]->MaterialPtr;
        expect(captured&&id&&opaqueSaved&&assigned&&first!=legacy&&second!=masked&&first->Surface&&second->Surface
            &&!first->SubsurfaceOnlySurface&&!second->SubsurfaceOnlySurface&&!first->bRaytracingAlphaTest&&!second->bRaytracingAlphaTest
            &&first->Surface->AlphaMode==jSurfaceAlphaMode::Opaque&&first->Surface->AlphaCutoff==.4f
            &&!second->Surface->DoubleSided&&!second->Surface->LegacyPackedMap,
            "real production adapter graph assignment publishes explicit opaque coverage after its alpha-enabled clone");
        expect(first->Surface&&second->Surface&&first->Surface->Subsurface==.35f&&first->Surface->SpecularWeight==3
            &&first->Surface->Transmission==0&&second->Surface->Transmission==.4f&&second->Surface->Ior==1.6f
            &&!legacy->Surface&&legacy->bRaytracingAlphaTest&&masked->Surface->AlphaMode==jSurfaceAlphaMode::Mask,
            "coverage publication preserves instance SSS Skin and unrelated imported surface fields without mutating shared originals");
        opaque.AlphaMode=jSurfaceAlphaMode::Mask;opaque.AlphaCutoff=.6f;opaque.DoubleSided=true;opaque.LegacyPackedMap=true;
        const bool maskSaved=jEditorTools::SaveMaterial(graph,directory/maskPath,error);
        entity=*document.Find(id);entity.Material.GraphAsset=maskPath;document.Update(entity,"Assign explicit masked graph");editor.Tick(0,nullptr);
        expect(maskSaved&&source.RenderObjects[0]->MaterialPtr->Surface->AlphaMode==jSurfaceAlphaMode::Mask
            &&source.RenderObjects[0]->MaterialPtr->bRaytracingAlphaTest&&source.RenderObjects[0]->MaterialPtr->RaytracingAlphaCutoff==.6f
            &&source.RenderObjects[1]->MaterialPtr->Surface->DoubleSided&&source.RenderObjects[1]->MaterialPtr->Surface->LegacyPackedMap,
            "explicit Mask graph publishes its cutoff double-sided and packed-map coverage through the same ordinary owner path");
        entity=*document.Find(id);entity.Material.GraphAsset.clear();entity.Material.GraphOverrides.reset();document.Update(entity,"Inherit original coverage");editor.Tick(0,nullptr);
        const auto inheritedLegacy=source.RenderObjects[0]->MaterialPtr,inheritedMask=source.RenderObjects[1]->MaterialPtr;
        expect(inheritedLegacy->Surface&&inheritedLegacy->SubsurfaceOnlySurface&&inheritedLegacy->bRaytracingAlphaTest
            &&inheritedLegacy->RaytracingAlphaCutoff==.3f&&inheritedLegacy->Surface->Subsurface==.35f
            &&inheritedMask->Surface&&inheritedMask->Surface->AlphaMode==jSurfaceAlphaMode::Mask
            &&inheritedMask->Surface->AlphaCutoff==.2f&&inheritedMask->bRaytracingAlphaTest&&!inheritedMask->SubsurfaceOnlySurface,
            "removing a graph restores each original coverage while preserving legacy absence and authored scattering-only metadata");
        const bool undoMask=document.Undo();editor.Tick(0,nullptr);const bool maskRestored=source.RenderObjects[0]->MaterialPtr->bRaytracingAlphaTest
            &&source.RenderObjects[0]->MaterialPtr->Surface->AlphaCutoff==.6f&&!source.RenderObjects[0]->MaterialPtr->SubsurfaceOnlySurface;
        const bool undoOpaque=document.Undo();editor.Tick(0,nullptr);
        expect(undoMask&&maskRestored&&undoOpaque&&!source.RenderObjects[0]->MaterialPtr->bRaytracingAlphaTest
            &&source.RenderObjects[0]->MaterialPtr->Surface->AlphaMode==jSurfaceAlphaMode::Opaque,
            "Undo republishes both Mask and Opaque graph coverage through the real adapter rather than leaving stale flags");
        editor.SelectEntity(id);const auto duplicate=editor.DuplicateSelected();editor.Tick(0,nullptr);
        if(duplicate){auto copy=*document.Find(duplicate);copy.Material.GraphAsset.clear();copy.Material.GraphOverrides.reset();document.Update(copy,"Inherit duplicate coverage");editor.Tick(0,nullptr);}
        const auto* copied=editor.ObjectForEntity(duplicate);
        expect(copied&&copied->RenderObjects[0]->MaterialPtr->SubsurfaceOnlySurface&&copied->RenderObjects[0]->MaterialPtr->bRaytracingAlphaTest
            &&copied->RenderObjects[0]->MaterialPtr->RaytracingAlphaCutoff==.3f
            &&!source.RenderObjects[0]->MaterialPtr->bRaytracingAlphaTest,
            "duplicated material bindings retain original coverage inheritance independently of the edited original entity");
        editor.ShutdownWorld();jObject::RemoveObject(&source);g_Editor=oldEditor;
    }
    {
        jPlacementTool placement;jObject grouped;
        for(int n=0;n<2;++n){auto* mesh=new jRenderObject();mesh->SetPos(Vector(n?10.0f:-10.0f,0,0));mesh->SetRot(Vector(0,0,n?0.3f:0.1f));mesh->SetScale(Vector(n?2.0f:1.0f));mesh->UpdateWorldMatrix();grouped.AddRenderObject(mesh);}
        placement.RegisterStaticObject(&grouped);placement.SelectObject(&grouped);Matrix before{IdentityType};placement.GetSelectionWorldMatrix(before);
        const Matrix a=grouped.RenderObjects[0]->World,b=grouped.RenderObjects[1]->World;
        const Matrix after=Matrix::MakeTranslate(30,4,5)*Matrix::MakeRotateZ(DegreeToRadian(90))*Matrix::MakeScale(Vector(2))*before;
        const Matrix delta=after*before.GetInverse();
        expect(placement.ApplySelectedWorldMatrix(before,after)&&MatrixNear(grouped.RenderObjects[0]->World,delta*a)
            &&MatrixNear(grouped.RenderObjects[1]->World,delta*b),"actual legacy gizmo delta preserves grouped mesh offsets orientations and scales");
        const auto unchanged=grouped.RenderObjects[0]->World;Matrix invalid=after;invalid.m00=std::numeric_limits<float>::quiet_NaN();
        expect(!placement.ApplySelectedWorldMatrix(after,invalid)&&MatrixNear(grouped.RenderObjects[0]->World,unchanged),"invalid group gizmo transform is rejected atomically");
    }
    {
        jEditor editor;g_Editor=&editor;editor.SetProjectRoot(directory);
        jObject sheared;auto* mesh=new jRenderObject();Matrix shear{IdentityType};shear.m10=0.5f;
        mesh->SetWorldMatrixOverride(shear);sheared.AddRenderObject(mesh);jObject::AddObject(&sheared);
        editor.TrackWorldObjectSource(&sheared,"Assets/fixture.gltf","sheared-source",1);
        expect(editor.CaptureWorldToDocument()&&!editor.EntityForObject(&sheared)&&editor.GetDocument().Entities().empty()
            &&editor.LastError().find("not captured")!=std::string::npos&&MatrixNear(mesh->World,shear),
            "non-TRS source capture rejects the authored record and preserves the real external matrix");
        editor.ShutdownWorld();jObject::RemoveObject(&sheared);g_Editor=oldEditor;
    }
    {
        jEditor editor;
        auto& document = editor.GetDocument();
        const auto parent = document.Create(EntityKind::Empty, "Terrain parent");
        auto group = *document.Find(parent);
        group.Local.Position = {40, 10, 30};
        group.Local.RotationDegrees.Y = 27;
        group.Local.Scale = {1.5f, .5f, 2};
        document.Update(group);
        const auto terrain = document.Create(EntityKind::Terrain, "Viewport terrain", parent);
        editor.SelectEntity(terrain);
        const Vector center(40, 10, 30), eye = center + Vector(0, 330, -500);
        std::unique_ptr<jCamera> camera(jCamera::CreateCamera(eye, center, eye + Vector::UpVector,
            DegreeToRadian(60), .1f, 2000, 1280, 720, true));
        camera->UpdateCamera();
        Matrix world;
        document.GetWorldMatrix(terrain, world.mm);
        const auto localToClip = camera->GetViewProjectionMatrix() * world;
        const auto screen = [&](float x)
        {
            const auto ndc = localToClip.TransformPoint(Vector(x, 0, 0));
            return ImVec2(640 * (ndc.x + 1), 360 * (1 - ndc.y));
        };
        const auto draw = [&](const ImVec2& mouse, bool down)
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1280, 720));
            ImGui::Begin("Terrain brush smoke");
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) jEditorTools::FinishTerrainStroke(editor);
            auto* list = ImGui::GetWindowDrawList();
            const int before = list->VtxBuffer.Size;
            const bool active = jEditorTools::RenderTerrainBrush(editor, *camera, ImVec2(0, 0), ImVec2(1280, 720), true);
            const bool drewPreview = active && list->VtxBuffer.Size > before;
            ImGui::End();
            ImGui::Render();
            return drewPreview;
        };
        const auto start = screen(0), end = screen(187.5f);
        expect(draw(start, false) && document.Find(terrain)->Terrain.Heights.empty(),
            "terrain brush hover draws a preview on a transformed terrain without editing");
        draw(start, true);
        const auto clickHeights = document.Find(terrain)->Terrain.Heights;
        // ImGui rounds pointer positions to whole pixels; allow the resulting brush falloff.
        const float centerHeight = clickHeights.size() == 289 ? clickHeights[8 * 17 + 8] : 0;
        expect(centerHeight > 19.5f && centerHeight <= 20,
            "viewport brush click edits the mesh hit in terrain-local coordinates");
        draw(end, true);
        draw(end, false);
        const auto dragHeights = document.Find(terrain)->Terrain.Heights;
        expect(dragHeights.size() == 289 && dragHeights[8 * 17 + 11] > 0 && dragHeights != clickHeights,
            "viewport brush drag paints beyond the initial click");
        expect(document.UndoLabel() == "Paint terrain stroke" && document.Undo() &&
            document.Find(terrain)->Terrain.Heights.empty(), "one undo restores the terrain before the whole viewport stroke");
        jEditorTools::Shutdown();
    }
    {
        jEditor editor;
        editor.SetProjectRoot(directory);
        auto& document = editor.GetDocument();
        const auto first = document.Create(EntityKind::Cube, "Inherited material");
        const auto second = document.Create(EntityKind::Cube, "Instance texture");
        const std::string path = "Assets/shared.jmaterial";
        jEditorTools::MaterialGraph graph;
        graph.Nodes = {{1, 0, 0, jEditorTools::NodeKind::Texture, {1, 1, 1}, 1, 0, 0, "Assets/default.png"},
            {2, 0, 0, jEditorTools::NodeKind::Scalar, {}, .4f},
            {3, 0, 0, jEditorTools::NodeKind::Texture, {1, 1, 1}, 1, 0, 0, "Assets/default-normal.png"}};
        graph.BaseColor = 1;
        graph.Roughness = 2;
        graph.Normal = 3;
        std::string error;
        expect(jEditorTools::SaveMaterial(graph, directory / path, error) &&
            jEditorTools::AssignSharedMaterial(editor, first, path, error) &&
            jEditorTools::AssignSharedMaterial(editor, second, path, error),
            "two editor objects can reference one saved material graph");
        auto variant = *document.Find(second);
        variant.Material.BaseColorTexture = "Assets/variant.png";
        variant.Material.NormalTexture = "Assets/variant-normal.png";
        if (auto& overrides = variant.Material.GraphOverrides)
        {
            overrides->BaseColorTexture = true;
            overrides->NormalTexture = true;
        }
        document.Update(variant, "Set instance texture");
        Document reopened;
        const bool roundTrip = reopened.LoadJson(document.ToJson());
        expect(roundTrip && reopened.Find(second)->Material == variant.Material,
            "material instance graph link and local parameter override survive scene reload");
        graph.Nodes[0].Texture = "Assets/new-default.png";
        graph.Nodes[1].Scalar = .8f;
        graph.Nodes[2].Texture = "Assets/new-default-normal.png";
        const bool saved = jEditorTools::SaveMaterial(graph, directory / path, error);
        editor.ReimportAsset(path);
        const auto a = editor.ResolveMaterial(document.Find(first)->Material);
        const auto b = editor.ResolveMaterial(document.Find(second)->Material);
        expect(saved && a.BaseColorTexture == "Assets/new-default.png" && b.BaseColorTexture == "Assets/variant.png" &&
            a.Roughness == .8f && b.Roughness == .8f &&
            a.NormalTexture == "Assets/new-default-normal.png" && b.NormalTexture == "Assets/variant-normal.png",
            "shared graph edits propagate to inherited parameters without replacing a local texture");
        { std::ofstream corrupted(directory / path); corrupted << "{}"; }
        editor.ReimportAsset(path);
        expect(editor.ResolveMaterial(document.Find(first)->Material) == a,
            "invalid shared graph reload preserves the last valid material");
        graph.Normal = 0;
        const bool disconnected = jEditorTools::SaveMaterial(graph, directory / path, error);
        editor.ReimportAsset(path);
        expect(disconnected && editor.ResolveMaterial(document.Find(first)->Material).NormalTexture.empty() &&
            editor.ResolveMaterial(document.Find(second)->Material).NormalTexture == "Assets/variant-normal.png",
            "disconnecting Normal clears the inherited graph texture and keeps the instance texture");
    }
    g_Editor=oldEditor;g_KeyState=oldKeys;g_MouseState=oldMouse;ImGui::DestroyContext(context);ImGui::SetCurrentContext(previousContext);
    std::error_code error;std::filesystem::remove_all(directory,error);
    return failures;
#else
    return 0;
#endif
}

int RunEditorParticleMaterialGpuTests()
{
#ifdef ENABLE_EDITOR_FEATURES
    int failures=0;
    const auto expect=[&](bool condition,const char* name){std::cout<<(condition?"PASS ":"FAIL ")<<name<<std::endl;failures+=!condition;};
    if(!g_rhi||!GDefaultMaterial){expect(false,"particle material GPU regression requires the real initialized RHI and default material");return failures;}
    auto* previousEditor=g_Editor;
    {
        jEditor editor;g_Editor=&editor;jEditorProcedural::Install(editor);
        const auto originalDefault=GDefaultMaterial;
        const auto originalColor=GDefaultMaterial->BaseColorOpacity;
        const auto originalEmissive=GDefaultMaterial->Emissive;
        const bool originalOverride=GDefaultMaterial->UseMaterialProperties;
        const auto originalRevision=GDefaultMaterial->Revision;
        const auto firstId=editor.CreateEntity(jEditorAuthoring::EntityKind::ParticleEmitter,"Warm emitter");
        const auto secondId=editor.CreateEntity(jEditorAuthoring::EntityKind::ParticleEmitter,"Cool emitter");
        auto first=*editor.GetDocument().Find(firstId),second=*editor.GetDocument().Find(secondId);
        first.Particles.Color={1,.2f,.1f};second.Particles.Color={.1f,.4f,1};
        editor.GetDocument().Update(first,"Warm particle color");editor.GetDocument().Update(second,"Cool particle color");
        editor.Tick(0,jCamera::GetMainCamera());
        auto* firstObject=editor.ObjectForEntity(firstId);auto* secondObject=editor.ObjectForEntity(secondId);
        const bool published=firstObject&&secondObject&&!firstObject->RenderObjects.empty()&&!secondObject->RenderObjects.empty();
        expect(published,"particle color regression publishes two real procedural GPU objects without substitute factories");
        if(published){
            auto* firstRender=firstObject->RenderObjects[0];auto* secondRender=secondObject->RenderObjects[0];
            const auto firstMaterial=firstRender->MaterialPtr,secondMaterial=secondRender->MaterialPtr;
            expect(firstMaterial&&secondMaterial&&firstMaterial->UseMaterialProperties&&secondMaterial->UseMaterialProperties
                &&firstMaterial!=secondMaterial&&firstMaterial!=originalDefault&&secondMaterial!=originalDefault
                &&firstMaterial->BaseColorOpacity.IsNearlyEqual(Vector4(1,.2f,.1f,1))
                &&secondMaterial->BaseColorOpacity.IsNearlyEqual(Vector4(.1f,.4f,1,1)),
                "particle colors reach distinct live authored materials while shared defaults stay copy-on-write");
            first=*editor.GetDocument().Find(firstId);first.Particles.Color={.2f,1,.3f};
            editor.GetDocument().Update(first,"Edit only warm emitter");editor.Tick(0,jCamera::GetMainCamera());
            expect(firstRender->MaterialPtr->BaseColorOpacity.IsNearlyEqual(Vector4(.2f,1,.3f,1))
                &&secondRender->MaterialPtr->BaseColorOpacity.IsNearlyEqual(Vector4(.1f,.4f,1,1))
                &&GDefaultMaterial==originalDefault&&GDefaultMaterial->BaseColorOpacity==originalColor
                &&GDefaultMaterial->Emissive==originalEmissive&&GDefaultMaterial->UseMaterialProperties==originalOverride
                &&GDefaultMaterial->Revision==originalRevision,
                "editing one real particle color preserves the other emitter and every captured default-material value");
            expect(!firstRender->IsSupportRaytracing()&&!secondRender->IsSupportRaytracing(),
                "real particle render objects keep the documented raster-only runtime contract");
        }else std::cout<<"DETAIL particle publication: "<<editor.LastError()<<std::endl;
        editor.ShutdownWorld();g_Editor=previousEditor;
    }
    return failures;
#else
    return 0;
#endif
}
