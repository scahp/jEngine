#include "pch.h"
#include "jTextureSubsurfacePass.h"
#include "Features/jTextureSubsurfaceMath.h"
#include "jDrawCommand.h"
#include "Material/jMaterial.h"
#include "Material/jMaterialMappingShader.h"
#include "Material/jMaterialWater.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jMeshObject.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jResourceBarrierBatcher.h"
#include "Shader/jTextureSubsurfaceShaderParameters.h"
#include "Profiler/jPerformanceProfile.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace jTextureSubsurface
{
namespace
{
DebugOutput LastDebug;
constexpr std::size_t MaximumScannedVertices=4u*1024u*1024u;
constexpr std::size_t MaximumCaptureDraws=512;

void Flush(const std::shared_ptr<jRenderFrameContext>& frame)
{
#if USE_RESOURCE_BARRIER_BATCHER
    auto* command=frame->GetActiveCommandBuffer();
    if(auto* batch=g_rhi->GetGlobalBarrierBatcher()) batch->Flush(command);
    command->FlushBarrierBatch();
#endif
}
template<class T> std::shared_ptr<IUniformBufferBlock> Uniform(const char* name,const T& value)
{
    auto result=g_rhi->CreateUniformBufferBlock(jName(name),jLifeTimeType::OneFrame,sizeof(T));
    if(result) result->UpdateBufferData(&value,sizeof(T));
    return result;
}
bool ValidTarget(const std::shared_ptr<jRenderTarget>& target,std::uint32_t w,std::uint32_t h)
{
    return target && target->GetTexture() && target->GetTexture()->GetHandle() && !target->Info.IsMemoryless &&
        target->Info.Type==ETextureType::TEXTURE_2D && target->Info.SampleCount==EMSAASamples::COUNT_1 &&
        target->Info.Width==int(w) && target->Info.Height==int(h);
}
std::shared_ptr<jRenderTarget> Target(const char* name,ETextureFormat format,int w,int h)
{
    const bool depth=IsDepthFormat(format);
    return jRenderTargetPool::GetRenderTargetForOneFrame({.Type=ETextureType::TEXTURE_2D,.Format=format,
        .Width=w,.Height=h,.SampleCount=EMSAASamples::COUNT_1,
        .RTClearValue=depth?jRTClearValue(1.f,0):jRTClearValue(0.f,0.f,0.f,0.f),
        .TextureCreateFlag=(depth?ETextureCreateFlag::DSV:ETextureCreateFlag::RTV|ETextureCreateFlag::UAV)|ETextureCreateFlag::TransferSrc,
        .ResourceName=jName(name)});
}
jRenderPass* RenderPass(const std::vector<std::shared_ptr<jRenderTarget>>& colors,
    const std::shared_ptr<jRenderTarget>& depth,int w,int h)
{
    jRenderPassInfo info;info.SynchronizeDepthStencil=bool(depth);jSubpass subpass;
    for(const auto& target:colors)
    {
        subpass.OutputColorAttachments.push_back(int(info.Attachments.size()));
        info.Attachments.push_back({.RenderTargetPtr=target,.LoadStoreOp=EAttachmentLoadStoreOp::CLEAR_STORE,
            .StencilLoadStoreOp=EAttachmentLoadStoreOp::DONTCARE_DONTCARE,.RTClearValue=target->Info.RTClearValue,
            .InitialLayout=EResourceLayout::COLOR_ATTACHMENT,.FinalLayout=EResourceLayout::COLOR_ATTACHMENT});
    }
    if(depth)
    {
        subpass.OutputDepthAttachment=int(info.Attachments.size());
        info.Attachments.push_back({.RenderTargetPtr=depth,.LoadStoreOp=EAttachmentLoadStoreOp::CLEAR_STORE,
            .StencilLoadStoreOp=EAttachmentLoadStoreOp::DONTCARE_DONTCARE,.RTClearValue=jRTClearValue(1.f,0),
            .InitialLayout=EResourceLayout::DEPTH_STENCIL_ATTACHMENT,.FinalLayout=EResourceLayout::DEPTH_STENCIL_ATTACHMENT});
    }
    info.Subpasses.push_back(subpass);
    return g_rhi->GetOrCreateRenderPass(info,{0,0},{w,h});
}
struct Attribute
{
    const unsigned char* Bytes=nullptr;
    std::size_t Stride=0,Offset=0,Size=0;
};
bool FindAttribute(const jVertexStreamData& stream,std::uint32_t wanted,std::size_t floats,Attribute& output)
{
    std::uint32_t location=std::uint32_t(stream.StartLocation);
    for(const auto& parameter:stream.Params)
    {
        if(!parameter) return false;
        std::size_t offset=0;
        for(const auto& attribute:parameter->Attributes)
        {
            if(location++==wanted)
            {
                if(attribute.UnderlyingType!=EBufferElementType::FLOAT || attribute.Stride!=int(floats*sizeof(float)) ||
                    parameter->Stride<=0 || parameter->InstanceDivisor || parameter->GetBufferSize()<offset+floats*sizeof(float)) return false;
                output={static_cast<const unsigned char*>(parameter->GetBufferData()),std::size_t(parameter->Stride),offset,parameter->GetBufferSize()};
                return output.Bytes && output.Offset+floats*sizeof(float)<=output.Stride;
            }
            if(attribute.Stride<=0) return false;
            offset+=std::size_t(attribute.Stride);
        }
    }
    return false;
}
bool ReadAttribute(const Attribute& attribute,std::size_t index,float* output,std::size_t count)
{
    if(index>(std::numeric_limits<std::size_t>::max()-attribute.Offset)/attribute.Stride ||
        index*attribute.Stride+attribute.Offset+count*sizeof(float)>attribute.Size) return false;
    std::memcpy(output,attribute.Bytes+index*attribute.Stride+attribute.Offset,count*sizeof(float));
    for(std::size_t c=0;c<count;++c) if(!std::isfinite(output[c])) return false;
    return true;
}
bool Opaque(const jMaterial& material,std::string& error)
{
    const bool graph=material.MappingProgram&&material.MappingProgram->Active;
    const bool explicitCoverage=material.Surface&&!material.SubsurfaceOnlySurface;
    if(material.IsUseSphericalMap() || (material.Water && material.Water->Enabled) ||
        !std::isfinite(material.BaseColorOpacity.w) || material.BaseColorOpacity.w<.999f ||
        (explicitCoverage && material.Surface->AlphaMode!=jSurfaceAlphaMode::Opaque) ||
        (!explicitCoverage && (material.HasAlbedoTexture()||graph)) ||
        (material.MappingProgram && (material.MappingProgram->UsesHorizon || material.MappingProgram->UsesInterior)))
    {
        error="Texture transport requires static opaque UV surfaces/blockers; alpha, water, spherical UV or horizon/interior coverage uses screen fallback.";
        return false;
    }
    return true;
}
bool Geometry(const jRenderObject& object,bool checkUV,std::size_t& scanned,Vector& minimum,Vector& maximum,std::string& error)
{
    const auto& geometry=object.GeometryDataPtr;
    if(!geometry || !geometry->VertexBufferPtr || !geometry->VertexStreamPtr || geometry->HasInstancing() ||
        geometry->IndirectCommandBufferPtr || geometry->GetPrimitiveType()!=EPrimitiveType::TRIANGLES)
    {error="Texture transport requires retained non-instanced triangle geometry and direct submesh draws.";return false;}
    const auto& stream=*geometry->VertexStreamPtr;
    if(stream.StartLocation!=0 || stream.ElementCount<=0)
    {error="Texture transport vertex stream is empty or has a nonstandard first location.";return false;}
    std::size_t first=0,count=std::size_t(stream.ElementCount);
    if(const auto* element=dynamic_cast<const jRenderObjectElement*>(&object))
    {
        if(element->SubMesh.StartVertex<0 || element->SubMesh.EndVertex<=element->SubMesh.StartVertex ||
            element->SubMesh.EndVertex>stream.ElementCount)
        {error="Texture transport submesh vertex range is invalid.";return false;}
        first=std::size_t(element->SubMesh.StartVertex);count=std::size_t(element->SubMesh.EndVertex)-first;
    }
    const auto color=geometry->HasVertexColor()?1u:0u,bitangent=geometry->HasVertexBiTangent()?1u:0u;
    Attribute position,normal,tangent,uv,vertexColor;
    if(!FindAttribute(stream,0,3,position) || !FindAttribute(stream,1+color,3,normal) ||
        !FindAttribute(stream,2+color,3,tangent) || !FindAttribute(stream,3+color+bitangent,2,uv) ||
        (color && !FindAttribute(stream,1,4,vertexColor)))
    {error="Texture transport requires the standard position/normal/tangent/UV vertex layout.";return false;}
    if(count>MaximumScannedVertices-scanned)
    {error="Texture transport exceeded its four-million retained-vertex CPU validation budget.";return false;}
    scanned+=count;
    for(std::size_t i=first;i<first+count;++i)
    {
        float p[3],texcoord[2],rgba[4];
        if(!ReadAttribute(position,i,p,3) || !ReadAttribute(uv,i,texcoord,2) ||
            (color && (!ReadAttribute(vertexColor,i,rgba,4) || rgba[3]<.999f)))
        {error="Texture transport contains invalid CPU vertex data or fractional vertex coverage.";return false;}
        if(checkUV && (texcoord[0]<0.f || texcoord[1]<0.f || texcoord[0]>1.f || texcoord[1]>1.f))
        {error="Texture transport requires a finite non-repeating UV chart in [0,1]; screen fallback.";return false;}
        if(checkUV)
        {
            const Vector point=object.World.TransformPoint(Vector(p[0],p[1],p[2]));
            if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))
            {error="Texture transport contains nonfinite actual transformed positions.";return false;}
            minimum.x=std::min(minimum.x,point.x);minimum.y=std::min(minimum.y,point.y);minimum.z=std::min(minimum.z,point.z);
            maximum.x=std::max(maximum.x,point.x);maximum.y=std::max(maximum.y,point.y);maximum.z=std::max(maximum.z,point.z);
        }
    }
    return true;
}
bool FitLight(Light& light,const Vector& minimum,const Vector& maximum,std::string& error)
{
    if(!std::isfinite(light.MaxDistance)||light.MaxDistance<0.f)
    {error="Texture transport key has invalid maximum distance.";return false;}
    if(!light.AutoFitCapture)
    {
        for(float value:light.CaptureVP.mm) if(!std::isfinite(value))
        {error="Texture transport explicit light projection is nonfinite.";return false;}
        return true;
    }
    const Vector center=(minimum+maximum)*.5f;
    const float radius=(maximum-minimum).Length()*.5f;
    if(!std::isfinite(radius)||radius<=1.e-5f)
    {error="Texture transport selected bounds are empty or degenerate.";return false;}
    Vector eye=Vector(light.PositionAndReferenceDistance.x,light.PositionAndReferenceDistance.y,light.PositionAndReferenceDistance.z);
    Vector ray;
    Matrix projection;
    if(light.ColorAndType.w>.5f)
    {
        const float distance=(center-eye).Length();
        if(!std::isfinite(distance)||distance<=radius*1.21f)
        {error="Texture transport finite key is inside/too near selected bounds; screen fallback.";return false;}
        const float expanded=radius*1.2f;
        const float fov=std::min(3.0f,2.f*std::asin(std::min(expanded/distance,.99f))*1.1f);
        projection=jCameraUtil::CreatePerspectiveMatrix(1.f,1.f,fov,std::max(.001f,distance-expanded),distance+expanded);
        light.DirectionAndFar.w=distance+expanded;
        ray=(center-eye).GetNormalize();
    }
    else
    {
        ray=Vector(light.DirectionAndFar.x,light.DirectionAndFar.y,light.DirectionAndFar.z);
        if(!std::isfinite(ray.x)||!std::isfinite(ray.y)||!std::isfinite(ray.z)||ray.Length()<1.e-6f)
        {error="Texture transport directional key has no finite ray direction.";return false;}
        ray=ray.GetNormalize();eye=center-ray*(radius*2.4f);
        light.PositionAndReferenceDistance=Vector4(eye,1.f);
        light.DirectionAndFar=Vector4(ray,radius*4.8f);
        projection=jCameraUtil::CreateOrthogonalMatrix(radius*2.4f,radius*2.4f,.001f,radius*4.8f);
    }
    const Vector up=std::abs(ray.y)<.98f?Vector(0,1,0):Vector(1,0,0);
    const Matrix view=jCameraUtil::CreateViewMatrix(eye,center,eye+up);
    light.CaptureVP=projection*view;
    for(float value:light.CaptureVP.mm) if(!std::isfinite(value))
    {error="Texture transport fitted light projection is nonfinite.";return false;}
    return true;
}
struct Surface {jRenderObject* Object=nullptr;jMaterial* Material=nullptr;};
struct PreparedRaster {std::vector<jPipelineStateFixedInfo> States;std::vector<jDrawCommand> Draws;};
jGraphicsPipelineShader RasterShaders(const jRenderObject& object,jMaterial* material,bool capture)
{
    jGraphicsPipelineShader output;
    const auto& geometry=*object.GeometryDataPtr;
    const std::string defines=std::string("#define JE_TEXTURE_SSS_CAPTURE ")+(capture?"1\n":"0\n")+
        "#define USE_VERTEX_COLOR "+(geometry.HasVertexColor()?"1\n":"0\n")+
        "#define USE_VERTEX_BITANGENT "+(geometry.HasVertexBiTangent()?"1\n":"0\n")+
        "#define USE_ALBEDO_TEXTURE "+(material->HasAlbedoTexture()?"1\n":"0\n")+
        "#define USE_SRGB_ALBEDO_TEXTURE "+(material->IsUseSRGBAlbedoTexture()?"1\n":"0\n");
    for(int stage=0;stage<2;++stage)
    {
        jShaderInfo info;info.SetName(jName(std::string("TextureSubsurface")+(capture?"Capture":"Bake")+(stage?"PS":"VS")));
        info.SetShaderFilepath(stage?jNameStatic("Resource/Shaders/hlsl/texture_subsurface_ps.hlsl"):
            jNameStatic("Resource/Shaders/hlsl/texture_subsurface_vs.hlsl"));
        info.SetEntryPoint(jNameStatic("main"));info.SetPreProcessors(jName(defines));
        info.SetShaderType(stage?EShaderAccessStageFlag::FRAGMENT:EShaderAccessStageFlag::VERTEX);
        jShaderParameterSet::AppendToShaderInfo<jTextureSubsurfaceRasterParameters>(info,0);
        jShaderParameterSet::AppendToShaderInfo<jRenderObjectShaderParameters>(info,1);
        jShaderParameterSet::AppendToShaderInfo<jMaterialShaderParameters>(info,2);
        auto* shader=g_rhi->CreateShader(info);
        if(stage) shader=ResolveMaterialMappingShader(shader,material);
        if(stage) output.PixelShader=shader;else output.VertexShader=shader;
    }
    return output;
}
bool PrepareRaster(PreparedRaster& prepared,const std::shared_ptr<jRenderFrameContext>& frame,jRenderPass* pass,
    const std::vector<Surface>& objects,const std::map<jRenderObject*,std::uint32_t>& selected,
    jTextureSubsurfaceData value,const DebugOutput& targets,const Light& light,bool capture,std::string& error)
{
    prepared.States.reserve(objects.size());prepared.Draws.reserve(objects.size());
    auto* raster=TRasterizationStateInfo<EPolygonMode::FILL,ECullMode::NONE>::Create();
    auto* depth=capture?TDepthStencilStateInfo<true,true,ECompareOp::LEQUAL>::Create():TDepthStencilStateInfo<false,false>::Create();
    auto* blend=capture?TBlendingStateInfo<false>::Create():
        TBlendingStateInfo<true,EBlendFactor::ONE,EBlendFactor::ONE,EBlendOp::ADD,
            EBlendFactor::ONE,EBlendFactor::ONE,EBlendOp::ADD,EColorMask::ALL>::Create();
    for(const auto& object:objects)
    {
        const auto found=selected.find(object.Object);
        const auto tile=found==selected.end()?0u:found->second;
        const int resolution=capture?int(value.CaptureSizeAndInvSize.x):int(value.Atlas.z);
        const int x=capture?0:int(tile%std::uint32_t(value.Atlas.x))*resolution;
        const int y=capture?0:int(tile/std::uint32_t(value.Atlas.x))*resolution;
        prepared.States.emplace_back(raster,depth,blend,jViewport(x,y,resolution,resolution),jScissor(x,y,resolution,resolution),false);
        const auto id=object.Object->RenderObjectID;
        value.Draw=Vector4(float(tile),float(id&1023u),float(id>>10u),found!=selected.end()?1.f:0.f);
        jTextureSubsurfaceRasterParameters parameters;
        parameters.TextureSSS.Buffer=Uniform("TextureSubsurfaceRaster",value);
        if(!parameters.TextureSSS.Buffer){error="Texture transport raster uniform allocation failed.";return false;}
        // The capture shaders do not read their bound destinations. UV bake
        // executes only after the completed capture transitions to read-only.
        parameters.TextureSSSCapture0={targets.Capture0->GetTexture()};
        parameters.TextureSSSCapture1={targets.Capture1->GetTexture()};
        parameters.TextureSSSEnergyLUT={light.EnergyLUT.get()};
        auto binding=jShaderParameterSet::CreateShaderBindingInstance(parameters,EShaderAccessStageFlag::ALL_GRAPHICS,jShaderBindingInstanceType::SingleFrame);
        if(!binding){error="Texture transport raster bindings failed.";return false;}
        const auto shaders=RasterShaders(*object.Object,object.Material,capture);
        if(!shaders.VertexShader||!shaders.PixelShader||!shaders.VertexShader->GetCompiledShader()||!shaders.PixelShader->GetCompiledShader())
        {error="Texture transport raster shader failed.";return false;}
        jShaderBindingInstanceGroup bindings;bindings.Add(binding);
        bindings.Add(object.Object->CreateShaderBindingInstance());bindings.Add(object.Material->CreateShaderBindingInstance());
        // A fresh manual command deliberately has no camera Hi-Z override.
        prepared.Draws.emplace_back(frame,object.Object,pass,shaders,&prepared.States.back(),object.Material,
            bindings,nullptr,nullptr,0,EDrawCommandBindingMode::Manual);
        prepared.Draws.back().PrepareToDraw(false);
        if(!prepared.Draws.back().CurrentPipelineStateInfo)
        {error="Texture transport raster pipeline failed.";return false;}
    }
    return true;
}
bool RecordRaster(const std::shared_ptr<jRenderFrameContext>& frame,jRenderPass* pass,PreparedRaster& draws,
    const std::vector<std::shared_ptr<jRenderTarget>>& colors,const std::shared_ptr<jRenderTarget>& depth)
{
    auto* command=frame->GetActiveCommandBuffer();
    for(const auto& color:colors) g_rhi->TransitionLayout(command,color->GetTexture(),EResourceLayout::COLOR_ATTACHMENT);
    if(depth) g_rhi->TransitionLayout(command,depth->GetTexture(),EResourceLayout::DEPTH_STENCIL_ATTACHMENT);
    Flush(frame);
    if(!pass->BeginRenderPass(command)) return false;
    for(const auto& draw:draws.Draws) draw.Draw();
    pass->EndRenderPass();
    for(const auto& color:colors) g_rhi->TransitionLayout(command,color->GetTexture(),EResourceLayout::SHADER_READ_ONLY);
    return true;
}
struct PreparedCompute
{
    jPipelineStateInfo* Pipeline=nullptr;jShaderBindingInstanceGroup Bindings;
    jTexture* Input=nullptr; jTexture* Output=nullptr;
    std::uint32_t Width=0,Height=0;
};
template<class TParameters>
bool PrepareComputePhase(PreparedCompute& pass,int mode,const jTextureSubsurfaceData& value,
    const DebugOutput& targets,const Inputs& inputs,jTexture* source,jTexture* destination,
    std::uint32_t width,std::uint32_t height,std::string& error)
{
    if(TParameters::GetShaderParameterMembers().size()>std::size_t(jShaderBindingArray::NumOfInlineData))
    {error="Texture transport phase exceeds the runtime inline binding capacity; screen fallback.";return false;}
    jShaderInfo info;info.SetName(jName("TextureSubsurfaceCompute"+std::to_string(mode)));
    info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/texture_subsurface_cs.hlsl"));info.SetEntryPoint(jNameStatic("main"));
    info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    info.SetPreProcessors(jName("#define JE_TEXTURE_SSS_PASS "+std::to_string(mode)+"\n"));
    jShaderParameterSet::AppendToShaderInfo<TParameters>(info,0);
    auto* shader=g_rhi->CreateShader(info);
    if(!shader||!shader->GetCompiledShader()){error="Texture transport compute shader failed.";return false;}
    TParameters parameters;
    parameters.TextureSSS.Buffer=Uniform("TextureSubsurfaceCompute",value);
    if(!parameters.TextureSSS.Buffer){error="Texture transport compute uniform allocation failed.";return false;}
    parameters.TextureSSSStretch={targets.Stretch->GetTexture()};parameters.TextureSSSEntryUV={targets.EntryUV->GetTexture()};
    parameters.TextureSSSResult={destination};
    parameters.TextureSSSSurfaceFlags={targets.SurfaceFlags->GetTexture()};
    if constexpr(std::is_same_v<TParameters,jTextureSubsurfaceFilterParameters>)
    {
        parameters.TextureSSSInput={source};parameters.TextureSSSLevel0={targets.Irradiance[0]->GetTexture()};
        parameters.TextureSSSFinish={targets.Finish->GetTexture()};
    }
    else if constexpr(std::is_same_v<TParameters,jTextureSubsurfaceFinishParameters>)
    {
        parameters.TextureSSSLevel0={targets.Irradiance[0]->GetTexture()};
        parameters.TextureSSSLevel1={targets.Irradiance[1]->GetTexture()};
        parameters.TextureSSSLevel2={targets.Irradiance[2]->GetTexture()};
        parameters.TextureSSSLevel3={targets.Irradiance[3]->GetTexture()};
        parameters.TextureSSSLevel4={targets.Irradiance[4]->GetTexture()};
        parameters.TextureSSSLevel5={targets.Irradiance[5]->GetTexture()};
        parameters.TextureSSSFinish={targets.Finish->GetTexture()};parameters.TextureSSSCoverage={targets.Coverage->GetTexture()};
    }
    else
    {
        parameters.TextureSSSInput={source};parameters.TextureSSSScene={inputs.SceneAfterScreen->GetTexture()};
        parameters.TextureSSSScreenDiffuse={inputs.ScreenFilteredDiffuse?inputs.ScreenFilteredDiffuse->GetTexture():inputs.OriginalDiffuse->GetTexture()};
        parameters.TextureSSSAlbedo={inputs.Albedo->GetTexture()};parameters.TextureSSSMask={inputs.Mask->GetTexture()};
        parameters.TextureSSSSkinUV={inputs.SkinUV->GetTexture()};
        parameters.TextureSSSCompletedDiffuse={targets.CompletedDiffuse->GetTexture()};
    }
    auto binding=jShaderParameterSet::CreateShaderBindingInstance(parameters,EShaderAccessStageFlag::COMPUTE,jShaderBindingInstanceType::SingleFrame);
    if(!binding){error="Texture transport compute bindings failed.";return false;}
    pass.Bindings.Add(binding);pass.Pipeline=g_rhi->CreateComputePipelineStateInfo(shader,pass.Bindings.GetLayoutArray(),{});
    if(!pass.Pipeline){error="Texture transport compute pipeline failed.";return false;}
    pass.Input=source;pass.Output=destination;pass.Width=width;pass.Height=height;return true;
}
bool PrepareCompute(PreparedCompute& pass,int mode,const jTextureSubsurfaceData& value,
    const DebugOutput& targets,const Inputs& inputs,jTexture* source,jTexture* destination,
    std::uint32_t width,std::uint32_t height,std::string& error)
{
    if(mode<=4) return PrepareComputePhase<jTextureSubsurfaceFilterParameters>(pass,mode,value,targets,inputs,source,destination,width,height,error);
    if(mode==5) return PrepareComputePhase<jTextureSubsurfaceFinishParameters>(pass,mode,value,targets,inputs,source,destination,width,height,error);
    return PrepareComputePhase<jTextureSubsurfaceComposeParameters>(pass,mode,value,targets,inputs,source,destination,width,height,error);
}
}

Result BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame,const jView& view,const Inputs& inputs,const Settings& supplied)
{
    LastDebug={};Result result;result.Output=inputs.SceneAfterScreen;Settings settings;
    if(!ValidateSettings(supplied,settings,result.Diagnostic)) return result;
    if(!std::isfinite(inputs.Strength)||inputs.Strength<0.f||inputs.Strength>1.f)
    {result.Diagnostic="Texture transport strength is outside [0,1].";return result;}
    if(inputs.Strength==0.f) return result;
    if(!frame||!g_rhi||!frame->GetActiveCommandBuffer()||!view.Camera)
    {result.Diagnostic="Texture transport requires an active owner-thread frame/view.";return result;}
    if(!inputs.SceneAfterScreen)
    {result.Diagnostic="Texture transport has no stored scene input.";return result;}
    const auto width=std::uint32_t(inputs.SceneAfterScreen->Info.Width),height=std::uint32_t(inputs.SceneAfterScreen->Info.Height);
    for(const auto& target:{inputs.SceneAfterScreen,inputs.OriginalDiffuse,inputs.Albedo,inputs.Mask,inputs.SkinUV})
        if(!ValidTarget(target,width,height))
        {result.Diagnostic="Texture transport requires matching stored single-sample scene/diffuse/albedo/mask/UV metadata.";return result;}
    if(inputs.ScreenFilteredDiffuse&&!ValidTarget(inputs.ScreenFilteredDiffuse,width,height))
    {result.Diagnostic="Texture transport screen diffuse dimensions are inconsistent.";return result;}
    Light light=inputs.Key;
    if(!light.EnergyLUT||!light.EnergyLUT->GetHandle())
    {result.Diagnostic="Texture transport requires the matching Kelemen energy LUT.";return result;}
    for(const auto vector:{light.PositionAndReferenceDistance,light.ColorAndType,light.DirectionAndFar})
        for(float component:vector.v) if(!std::isfinite(component))
        {result.Diagnostic="Texture transport light is nonfinite.";return result;}
    if(light.PositionAndReferenceDistance.w<=0.f)
    {result.Diagnostic="Texture transport finite-key reference distance must be positive.";return result;}
    std::vector<Surface> surfaces,capture;std::set<jRenderObject*> seen;
    std::map<jRenderObject*,std::uint32_t> selected;
    Vector minimum(std::numeric_limits<float>::max()),maximum(-std::numeric_limits<float>::max());
    std::size_t scanned=0;
    for(const auto& draw:inputs.SkinDraws)
    {
        auto* object=draw.RenderObject;if(!object||!seen.insert(object).second) continue;
        auto* material=draw.Material?draw.Material:(object->MaterialPtr?object->MaterialPtr.get():GDefaultMaterial.get());
        if(!material||!material->Surface) continue;
        const auto& surface=*material->Surface;
        if(surface.ShadingModel!=jSurfaceShadingModel::SkinKelemen||surface.SubsurfaceSpace!=jSubsurfaceSpace::Texture) continue;
        if(draw.OverrideInstanceData)
        {result.Diagnostic="Texture transport does not support per-draw instance-data overrides; screen fallback.";return result;}
        if(surface.SubsurfaceProfile!=0u && !(material->MappingProgram&&material->MappingProgram->Active))
        {result.Stats.Surfaces.push_back({object->RenderObjectID,false,"UV six-lobe transport supports Skin profile0; this surface retains screen diffusion."});continue;}
        if(!(material->MappingProgram&&material->MappingProgram->Active) && (surface.Subsurface<=0.f||surface.SubsurfaceThicknessWorld<=0.f)) continue;
        if(!object->RenderObjectID||object->RenderObjectID>MaximumSurfaceID||surfaces.size()>=MaximumSurfaces)
        {result.Diagnostic="Texture transport exceeds four surfaces or exact 20-bit render-object IDs; screen fallback.";return result;}
        if(!Opaque(*material,result.Diagnostic)||!Geometry(*object,true,scanned,minimum,maximum,result.Diagnostic)) return result;
        selected.emplace(object,std::uint32_t(surfaces.size()));surfaces.push_back({object,material});
        result.Stats.Surfaces.push_back({object->RenderObjectID,true,"Skin profile0; other graph profile pixels retain screen diffusion."});
    }
    if(surfaces.empty()){result.Diagnostic="Texture transport has no supported active Skin/Texture surface; screen result retained.";return result;}
    if(!FitLight(light,minimum,maximum,result.Diagnostic)) return result;
    seen.clear();
    for(const auto& draw:inputs.CaptureDraws)
    {
        auto* object=draw.RenderObject;if(!object) continue;
        // A selected surface needs its own entry even when it does not cast
        // ordinary shadows. Other objects follow actual authored caster state.
        if(!selected.contains(object)&&object->Owner&&object->Owner->SkipShadowMapGen) continue;
        if(!seen.insert(object).second) continue;
        if(draw.OverrideInstanceData)
        {result.Diagnostic="Texture transport capture does not support per-draw instance-data overrides; screen fallback.";return result;}
        auto* material=draw.Material?draw.Material:(object->MaterialPtr?object->MaterialPtr.get():GDefaultMaterial.get());
        if(capture.size()>=MaximumCaptureDraws || !material || !Opaque(*material,result.Diagnostic))
        {if(result.Diagnostic.empty()) result.Diagnostic="Texture transport exceeds its bounded opaque-capture draw budget.";return result;}
        // Selected geometry was already checked while collecting exact bounds.
        if(!selected.contains(object)&&!Geometry(*object,false,scanned,minimum,maximum,result.Diagnostic)) return result;
        capture.push_back({object,material});
    }
    for(const auto& surface:surfaces) if(seen.insert(surface.Object).second) capture.push_back(surface);
    result.Stats.SurfaceCount=std::uint32_t(surfaces.size());result.Stats.CaptureDrawCount=std::uint32_t(capture.size());
    if(!ValidateWorkExtent(result.Stats.SurfaceCount,settings.TileResolution,settings.CaptureResolution,width,height,
        result.Stats.AdditionalTargetBytes,result.Diagnostic)) return result;
    const auto columns=surfaces.size()>1?2u:1u,rows=surfaces.size()>2?2u:1u;
    const auto aw=columns*settings.TileResolution,ah=rows*settings.TileResolution;
    result.Stats.AtlasWidth=aw;result.Stats.AtlasHeight=ah;
    DebugOutput targets;targets.Stats=result.Stats;targets.FrameNumber=g_rhi->GetCurrentFrameNumber();
    for(auto& target:targets.Irradiance) target=Target("TextureSubsurfaceIrradiance",ETextureFormat::RGBA16F,aw,ah);
    targets.Stretch=Target("TextureSubsurfaceStretch",ETextureFormat::RGBA16F,aw,ah);
    targets.EntryUV=Target("TextureSubsurfaceEntryUV",ETextureFormat::RGBA16F,aw,ah);
    targets.Finish=Target("TextureSubsurfaceFinish",ETextureFormat::RGBA16F,aw,ah);
    targets.FinalUV=Target("TextureSubsurfacePingFinal",ETextureFormat::RGBA16F,aw,ah);
    targets.Coverage=Target("TextureSubsurfaceCoverage",ETextureFormat::RGBA16F,aw,ah);
    targets.Capture0=Target("TextureSubsurfaceLightEntry",ETextureFormat::RGBA32F,settings.CaptureResolution,settings.CaptureResolution);
    targets.Capture1=Target("TextureSubsurfaceLightIdentity",ETextureFormat::RGBA16F,settings.CaptureResolution,settings.CaptureResolution);
    auto depth=Target("TextureSubsurfaceLightDepth",ETextureFormat::D32,settings.CaptureResolution,settings.CaptureResolution);
    targets.SurfaceFlags=Target("TextureSubsurfaceSurfaceFlags",ETextureFormat::R32UI,4,1);
    targets.Output=Target("TextureSubsurfaceOutput",ETextureFormat::RGBA16F,width,height);
    targets.CompletedDiffuse=Target("TextureSubsurfaceCompletedDiffuse",ETextureFormat::RGBA16F,width,height);
    targets.SkinUV=inputs.SkinUV;targets.Albedo=inputs.Albedo;
    for(const auto& target:targets.Irradiance) if(!target||!target->GetTexture()||!target->GetTexture()->GetHandle())
    {result.Diagnostic="Texture transport irradiance allocation failed; screen result retained.";return result;}
    for(const auto& target:{targets.Stretch,targets.EntryUV,targets.Finish,targets.FinalUV,targets.Coverage,
        targets.Capture0,targets.Capture1,depth,targets.SurfaceFlags,targets.Output,targets.CompletedDiffuse})
        if(!target||!target->GetTexture()||!target->GetTexture()->GetHandle())
        {result.Diagnostic="Texture transport target allocation failed; screen result retained.";return result;}
    jTextureSubsurfaceData values;
    // Engine Matrix/Vector default constructors deliberately do not initialize.
    // All unused surface slots and per-pass controls must have deterministic bits.
    std::memset(static_cast<void*>(&values),0,sizeof(values));values.LightVP=light.CaptureVP;
    values.InvLightVP=light.CaptureVP.GetInverse();
    for(float component:values.InvLightVP.mm) if(!std::isfinite(component))
    {result.Diagnostic="Texture transport light inverse is invalid; screen result retained.";return result;}
    values.KeyPositionAndReference=light.PositionAndReferenceDistance;values.KeyColorAndType=light.ColorAndType;
    values.DirectionAndFar=light.DirectionAndFar;
    values.Atlas=Vector4(float(columns),float(rows),float(settings.TileResolution),float(surfaces.size()));
    values.CaptureSizeAndInvSize=Vector4(float(settings.CaptureResolution),float(settings.CaptureResolution),1.f/settings.CaptureResolution,1.f/settings.CaptureResolution);
    values.Physical=Vector4(settings.WorldUnitsPerMillimeter,settings.TransmissionExtinctionPerMillimeter,settings.TransmissionThicknessScale,settings.CaptureBiasWorld);
    values.Features=Vector4(settings.EnableTransmission?1.f:0.f,settings.GaussianStepScale,inputs.Strength,light.MaxDistance);
    values.EyeWorld=Vector4(view.Camera->Pos,1.f);values.OutputSizeAndInvSize=Vector4(float(width),float(height),1.f/width,1.f/height);
    const auto weights=NormalizedLobeWeights();
    for(std::size_t i=0;i<weights.size();++i) values.Lobe[i]=Vector4(weights[i][0],weights[i][1],weights[i][2],VarianceMm2[i]);
    for(std::size_t i=0;i<surfaces.size();++i)
    {const auto id=surfaces[i].Object->RenderObjectID;values.SurfaceLookup[i]=Vector4(float(id&1023u),float(id>>10u),float(i%columns),float(i/columns));}
    const std::vector<std::shared_ptr<jRenderTarget>> captureTargets{targets.Capture0,targets.Capture1};
    const std::vector<std::shared_ptr<jRenderTarget>> bakeTargets{targets.Irradiance[0],targets.Stretch,targets.EntryUV,targets.Finish};
    auto* capturePass=RenderPass(captureTargets,depth,settings.CaptureResolution,settings.CaptureResolution);
    auto* bakePass=RenderPass(bakeTargets,nullptr,aw,ah);PreparedRaster captureDraws,bakeDraws;
    if(!capturePass||!bakePass||!PrepareRaster(captureDraws,frame,capturePass,capture,selected,values,targets,light,true,result.Diagnostic)||
        !PrepareRaster(bakeDraws,frame,bakePass,surfaces,selected,values,targets,light,false,result.Diagnostic)) return result;
    std::vector<PreparedCompute> compute;compute.reserve(24);
    const auto add=[&](int mode,jTexture* source,jTexture* dest,std::uint32_t w,std::uint32_t h)
    {compute.emplace_back();return PrepareCompute(compute.back(),mode,values,targets,inputs,source,dest,w,h,result.Diagnostic);};
    auto* ping=targets.FinalUV->GetTexture();auto* flags=targets.SurfaceFlags->GetTexture();
    if(!add(0,targets.Irradiance[0]->GetTexture(),ping,4,1)||
        !add(1,targets.Irradiance[0]->GetTexture(),ping,aw,ah)||
        !add(3,targets.Stretch->GetTexture(),targets.Coverage->GetTexture(),aw,ah)) return result;
    for(std::uint32_t level=1;level<6;++level)
    {
        values.Filter=Vector4(IncrementalSigmaMm(level),0.f,float(level),0.f);
        if(!add(2,targets.Irradiance[level-1]->GetTexture(),ping,aw,ah)) return result;
        values.Filter.y=1.f;
        if(!add(2,ping,targets.Irradiance[level]->GetTexture(),aw,ah)) return result;
        if(level<=4)
        {
            values.Filter.y=0.f;
            if(!add(4,targets.Coverage->GetTexture(),ping,aw,ah)) return result;
            values.Filter.y=1.f;
            if(!add(4,ping,targets.Coverage->GetTexture(),aw,ah)) return result;
        }
    }
    if(!add(5,targets.Irradiance[0]->GetTexture(),ping,aw,ah)||!add(6,ping,targets.Output->GetTexture(),width,height)) return result;
    SCOPE_GPU_PROFILE(frame,TextureSubsurfaceTransport);
    DEBUG_EVENT_WITH_COLOR(frame,"TextureSubsurfaceTransport",Vector4(.9f,.3f,.25f,1.f));
    if(!RecordRaster(frame,capturePass,captureDraws,captureTargets,depth)||!RecordRaster(frame,bakePass,bakeDraws,bakeTargets,nullptr))
    {result.Diagnostic="Texture transport render pass begin failed; screen result retained.";return result;}
    result.Stats.RasterPasses=2;
    auto* command=frame->GetActiveCommandBuffer();
    for(const auto& target:{inputs.SceneAfterScreen,inputs.OriginalDiffuse,inputs.ScreenFilteredDiffuse?inputs.ScreenFilteredDiffuse:inputs.OriginalDiffuse,
        inputs.Albedo,inputs.Mask,inputs.SkinUV}) g_rhi->TransitionLayout(command,target->GetTexture(),EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(command,flags,EResourceLayout::GENERAL);
    for(std::size_t i=0;i<compute.size();++i)
    {
        const auto& pass=compute[i];
        g_rhi->TransitionLayout(command,pass.Input,EResourceLayout::SHADER_READ_ONLY);
        if(i>1) g_rhi->TransitionLayout(command,pass.Output,EResourceLayout::GENERAL);
        if(i+1==compute.size()) g_rhi->TransitionLayout(command,targets.CompletedDiffuse->GetTexture(),EResourceLayout::GENERAL);
        Flush(frame);pass.Pipeline->Bind(frame);
        g_rhi->BindComputeShaderBindingInstances(command,pass.Pipeline,pass.Bindings.GetCombiner(),0);
        g_rhi->DispatchCompute(frame,(pass.Width+7u)/8u,(pass.Height+7u)/8u,1);
        ++result.Stats.ComputePasses;
        if(i<2) g_rhi->UAVBarrier(command,flags); // GENERAL->GENERAL atomic producer/consumer.
        else g_rhi->TransitionLayout(command,pass.Output,EResourceLayout::SHADER_READ_ONLY);
        if(i+1==compute.size()) g_rhi->TransitionLayout(command,targets.CompletedDiffuse->GetTexture(),EResourceLayout::SHADER_READ_ONLY);
    }
    result.Output=targets.Output;result.FilteredDiffuse=targets.CompletedDiffuse;result.Applied=true;
    result.Diagnostic="Texture transport: one-key UV six-lobe Skin profile0 + guarded front-entry TSM; other profiles retain screen diffusion. Overlap/chart bilinear footprints fall back locally; fatal capture/nonfinite flags retain the whole screen surface.";
    targets.Stats=result.Stats;LastDebug=std::move(targets);return result;
}

bool GetLastDebugOutput(DebugOutput& output)
{
    if(!g_rhi||LastDebug.FrameNumber!=g_rhi->GetCurrentFrameNumber()||!LastDebug.Output) return false;
    output=LastDebug;return true;
}
void ReleaseResources(){LastDebug={};}
}
