#include "pch.h"
#include "jEditorWaterTests.h"
#include "Code/Engine/Editor/jEditorWater.h"
#include "Material/jMaterial.h"
#include "Material/jMaterialWater.h"
#include "Shader/jCommonShaderParameters.h"
#include "External/PathTracingDataLoader/json.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <cstring>
#include <type_traits>

int RunEditorWaterTests()
{
    using namespace jEditorAuthoring;
    using Json=nlohmann::json;
    static_assert(int(EntityKind::Terrain)==9 && int(EntityKind::Water)==10);
    int checks=0,failures=0;
    const auto expect=[&](bool condition,const char* label)
    {++checks;if(!condition){++failures;std::cerr<<"EditorWater FAIL: "<<label<<'\n';}};
    const auto directory=std::filesystem::temp_directory_path()/("jengine-editor-water-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup
    {
        std::filesystem::path Path;
        ~Cleanup(){std::error_code ignored;std::filesystem::remove_all(Path,ignored);}
    } cleanup;
    try
    {
        if(!std::filesystem::create_directory(directory))throw std::runtime_error("Water fixture already exists.");
        cleanup.Path=directory;
        std::string error;
        WaterProperties defaults;
        expect(jEditorWater::Validate(defaults,error) && defaults.Resolution==33 && defaults.SizeX==1000
            && defaults.Waves.Waves.size()==4,"pond defaults are a valid rendered four-wave profile");
        {
            jMaterial neutral;
            bool constantsZero=true;
            for(const auto& value:neutral.MappingValues)constantsZero&=value==Vector4(0.f);
            expect(constantsZero,"fresh material mapping constants are explicitly zero despite Vector4's empty default constructor");
            static_assert(std::is_trivially_copyable_v<jMaterialGraphUniformBuffer>);
            static_assert(std::is_standard_layout_v<jMaterialGraphUniformBuffer>);
            const auto waterZero=[](const jMaterialGraphUniformBuffer& p)
            {
                if(!(p.WaterControl==Vector4(0.f)))return false;
                for(size_t i=0;i<4;++i)if(!(p.WaterWavesA[i]==Vector4(0.f))||!(p.WaterWavesB[i]==Vector4(0.f)))return false;
                return true;
            };
            const auto preserveOther=[](const jMaterialGraphUniformBuffer& a,const jMaterialGraphUniformBuffer& b)
            {
                return std::memcmp(&a,&b,offsetof(jMaterialGraphUniformBuffer,WaterWavesA))==0
                    &&a.MaterialSubsurfaceColorWeight==b.MaterialSubsurfaceColorWeight
                    &&a.MaterialSubsurfaceParameters==b.MaterialSubsurfaceParameters;
            };
            jMaterialGraphUniformBuffer parameters;
            for(int variant=0;variant<5;++variant)
            {
                std::memset(&parameters,0xa5,sizeof(parameters));
                parameters.Values[0]=Vector4(.2f,.4f,.6f,.8f);
                parameters.MaterialSubsurfaceColorWeight=Vector4(.8f,.4f,.2f,.7f);
                parameters.MaterialSubsurfaceParameters=Vector4(.5f,7,0,0);
                const auto before=parameters;
                jMaterialWaterState state;state.Settings=defaults.Waves;
                if(variant==1)state.Enabled=false;
                if(variant==2)state.TimeSeconds=std::numeric_limits<double>::quiet_NaN();
                if(variant==3)state.Settings.Waves.resize(5);
                if(variant==4)state.Settings.Waves[0].Direction={0,0};
                FillMaterialWaterShaderParameters(variant==0?nullptr:&state,parameters);
                expect(waterZero(parameters)&&preserveOther(parameters,before),
                    "missing disabled or invalid water clears poisoned wave/control bytes without clobbering graph constants masks or appended SSS data");
            }
            std::memset(&parameters,0xa5,sizeof(parameters));
            parameters.MaterialSubsurfaceColorWeight=Vector4(.8f,.4f,.2f,.7f);
            parameters.MaterialSubsurfaceParameters=Vector4(.5f,7,0,0);
            const auto before=parameters;
            jMaterialWaterState active;active.Settings=defaults.Waves;active.Settings.Waves.resize(1);
            active.Settings.BaseHeight=3;active.TimeSeconds=17.5;
            FillMaterialWaterShaderParameters(&active,parameters);
            jWaterSimulation::Settings published;published.BaseHeight=parameters.WaterControl.y;
            published.Waves.push_back({{parameters.WaterWavesA[0].x,parameters.WaterWavesA[0].y},
                parameters.WaterWavesA[0].z,parameters.WaterWavesA[0].w,parameters.WaterWavesB[0].x,
                parameters.WaterWavesB[0].y,parameters.WaterWavesB[0].z});
            jWaterSimulation::SurfaceSample originalSample,publishedSample;
            bool equivalent=jWaterSimulation::Evaluate(active.Settings,{11,19},active.TimeSeconds,originalSample,error)
                &&jWaterSimulation::Evaluate(published,{11,19},0,publishedSample,error);
            for(size_t i=0;i<3;++i)equivalent&=std::abs(originalSample.Position[i]-publishedSample.Position[i])<.0001f
                &&std::abs(originalSample.Normal[i]-publishedSample.Normal[i])<.0001f;
            expect(parameters.WaterControl==Vector4(1,3,0,1)&&equivalent&&preserveOther(parameters,before),
                "active water snapshot preserves the real analytic position and normal after owner-side temporal phase reduction");
            bool unusedZero=true;
            for(size_t i=1;i<4;++i)unusedZero&=parameters.WaterWavesA[i]==Vector4(0.f)&&parameters.WaterWavesB[i]==Vector4(0.f);
            expect(unusedZero,"active short wave lists clear every unused GPU wave slot");
            active.Enabled=false;FillMaterialWaterShaderParameters(&active,parameters);
            expect(waterZero(parameters)&&preserveOther(parameters,before),"disabling previously active water removes all deformation without touching SSS or mapping data");
        }
        Document legacy;
        const auto oldId=legacy.Create(EntityKind::Cube,"Legacy cube");
        const auto oldText=legacy.ToJson();
        expect(oldId && oldText.find("\"water\"")==std::string::npos,"unchanged non-water v1 payload omits optional water");
        Document reopenedLegacy;
        expect(reopenedLegacy.LoadJson(oldText) && reopenedLegacy.Find(oldId)->Water==defaults
            && reopenedLegacy.ToJson()==oldText,"old v1 scene opens and saves unchanged without water field");
        Document scene;
        const auto parent=scene.Create(EntityKind::Empty,"Pond group");
        const auto water=scene.Create(EntityKind::Water,"Authored pond",parent);
        expect(water && std::string(KindName(EntityKind::Water))=="Water","Water appended entity kind creates");
        auto entity=*scene.Find(water);
        entity.Water.Resolution=65;entity.Water.SizeX=700;entity.Water.SizeZ=900;
        entity.Water.Waves.BaseHeight=4;entity.Water.Waves.Waves[0].Amplitude=12;
        entity.Water.Waves.Waves[0].Wavelength=340;entity.Water.Waves.Waves[0].PhaseRadians=1.3f;
        entity.Local.Position={12,5,-8};
        expect(scene.Update(entity,"Author pond"),"authored dimensions/waves update");
        const auto path=directory/"pond.jscene";
        const auto beforeSave=scene.Entities();
        expect(scene.Save(path,false),"water scene saves");
        Document loaded;
        expect(loaded.Load(path) && loaded.Entities()==beforeSave && !loaded.IsDirty(),"water file round trip preserves IDs/hierarchy/material/wave data");
        const auto json=Json::parse(scene.ToJson());
        expect(json.at("version")==1 && json.at("entities")[1].at("water").at("waves").size()==4,
            "water extends schema v1 with explicit bounded wave array");
        auto missing=json;missing["entities"][1].erase("water");
        Document defaulted;
        expect(defaulted.LoadJson(missing.dump()) && defaulted.Find(water)->Water==defaults,
            "absent optional water properties use defaults");
        const auto originalWater=scene.Find(water)->Water;
        const auto originalRevision=scene.Revision();
        expect(scene.BeginTransaction("Water amplitude drag"),"water edit joins document transaction");
        for(int step=1;step<=3;++step)
        {
            auto edit=*scene.Find(water);edit.Water.Waves.Waves[0].Amplitude=12.f+step;
            expect(scene.Update(edit,"Water amplitude drag"),"water coalesced update accepted");
        }
        expect(scene.CommitTransaction(),"water coalesced transaction commits");
        const auto editedWater=scene.Find(water)->Water;
        expect(scene.Undo() && scene.Find(water)->Water==originalWater && scene.Revision()>originalRevision,
            "one undo restores full prior water profile with monotonic revision");
        expect(scene.Redo() && scene.Find(water)->Water==editedWater,"redo restores coalesced water profile");
        const auto duplicated=scene.Duplicate(water);
        expect(duplicated && duplicated!=water && scene.Find(duplicated)->Water==editedWater,"duplicate deep-copies wave data with fresh entity ID");
        auto duplicateEdit=*scene.Find(duplicated);duplicateEdit.Water.Waves.Waves[0].Amplitude+=1;
        expect(scene.Update(duplicateEdit) && scene.Find(water)->Water==editedWater,
            "editing duplicate waves does not mutate source profile");
        const auto prefab=scene.PrefabJson(water);
        const auto prefabId=scene.InstantiatePrefabJson(prefab,parent);
        expect(prefabId && prefabId!=water && scene.Find(prefabId)->Water==editedWater,
            "water prefab instantiates exact profile under selected parent");
        const auto clipboard=scene.SelectionJson({water,prefabId});
        const auto pasted=scene.PasteSelectionJson(clipboard,parent);
        expect(pasted.size()==2 && scene.Find(pasted[0])->Water==editedWater
            && scene.Find(pasted[1])->Water==editedWater,"water multi-selection clipboard preserves independent profiles");
        scene.Select(water);
        const auto beforePlay=scene.Entities();
        const auto beforePlayDirty=scene.IsDirty();
        expect(scene.BeginPlay(),"water play snapshot begins");
        auto playEdit=*scene.Find(water);playEdit.Water.Waves.BaseHeight+=100;
        const auto temporary=scene.Create(EntityKind::Water,"Temporary play water");
        expect(scene.Update(playEdit) && temporary,"play snapshot accepts temporary water runtime edits");
        expect(scene.StopPlay() && scene.Entities()==beforePlay && !scene.Find(temporary)
            && scene.SelectedId()==water && scene.IsDirty()==beforePlayDirty,
            "Stop restores water properties/history/selection and removes ephemeral entities");
        const auto retained=scene.Entities();
        const auto retainedRevision=scene.Revision();
        for(int variant=0;variant<8;++variant)
        {
            auto invalid=*scene.Find(water);
            if(variant==0)invalid.Water.Resolution=1;
            if(variant==1)invalid.Water.Resolution=130;
            if(variant==2)invalid.Water.SizeX=0;
            if(variant==3)invalid.Water.SizeZ=std::numeric_limits<float>::infinity();
            if(variant==4)invalid.Water.Waves.Waves.resize(5);
            if(variant==5)invalid.Water.Waves.Waves[0].Direction={0,0};
            if(variant==6)invalid.Water.Waves.Waves[0].Wavelength=0;
            if(variant==7){invalid.Water.Waves.Waves[0].Steepness=1;invalid.Water.Waves.Waves[0].Amplitude=1000;}
            expect(!scene.Update(invalid) && scene.Entities()==retained && scene.Revision()==retainedRevision,
                "invalid grid/parameters/GPU wave count rejected without document mutation");
        }
        for(int variant=0;variant<6;++variant)
        {
            auto corrupt=Json::parse(scene.ToJson());
            auto& waterJson=corrupt["entities"][1]["water"];
            if(variant==0)waterJson["resolution"]=129.5;
            if(variant==1)waterJson["waves"].push_back(waterJson["waves"][0]);
            if(variant==2)waterJson["waves"][0]["direction"]=Json::array({1,0,0});
            if(variant==3)waterJson["future"]=true;
            if(variant==4)waterJson["waves"][0]["future"]=true;
            if(variant==5)waterJson["waves"][0]["speed"]=1e100;
            expect(!scene.LoadJson(corrupt.dump()) && scene.Entities()==retained && scene.Revision()==retainedRevision,
                "corrupt water JSON rejects transactionally with strict field/bounds validation");
        }
        auto locked=*scene.Find(water);locked.Locked=true;expect(scene.Update(locked),"water locks");
        auto lockedEdit=*scene.Find(water);lockedEdit.Water.SizeX+=1;
        expect(!scene.Update(lockedEdit),"locked water cannot change authored dimensions");
        expect(scene.Undo() && !scene.Find(water)->Locked,"water lock participates in undo");
        const auto deleteId=scene.Duplicate(water);
        expect(scene.Delete(deleteId) && !scene.Find(deleteId) && scene.Undo() && scene.Find(deleteId)
            && scene.Find(deleteId)->Water==editedWater,"water delete/undo restores persistent profile");
    }
    catch(const std::exception& exception){expect(false,exception.what());}
    std::cout<<"Editor water tests: "<<checks-failures<<"/"<<checks<<" passed\n";
    return failures?1:0;
}
