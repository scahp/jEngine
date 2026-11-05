#!/usr/bin/env python3
"""Compile actual UV/TSM declarations/material graph to DXIL/SPIR-V; no engine/GPU build."""
import argparse
import importlib.util
import json
import struct
from pathlib import Path

EXPORTER = r'''
#define XXH_INLINE_ALL
#include "pch.h"
#include "Shader/jTextureSubsurfaceShaderParameters.h"
#include "Material/jMaterialMapping.cpp"
#include <iomanip>
static bool Vulkan=true;
bool IsUseVulkan(){return Vulkan;}
bool IsUseDX12(){return !Vulkan;}
struct BindingFixtureUniform final:IUniformBufferBlock
{
    void Init(size_t)override{} void Release()override{}
    void UpdateBufferData(const void*,size_t)override{} void ClearBuffer(int32=0)override{}
};
BEGIN_SHADER_PARAMETER_SET(OversizedBindingNegativeControl)
    SHADER_TEXTURE2D_SRV(T0)
    SHADER_TEXTURE2D_SRV(T1)
    SHADER_TEXTURE2D_SRV(T2)
    SHADER_TEXTURE2D_SRV(T3)
    SHADER_TEXTURE2D_SRV(T4)
    SHADER_TEXTURE2D_SRV(T5)
    SHADER_TEXTURE2D_SRV(T6)
    SHADER_TEXTURE2D_SRV(T7)
    SHADER_TEXTURE2D_SRV(T8)
    SHADER_TEXTURE2D_SRV(T9)
    SHADER_TEXTURE2D_SRV(T10)
    SHADER_TEXTURE2D_SRV(T11)
    SHADER_TEXTURE2D_SRV(T12)
    SHADER_TEXTURE2D_SRV(T13)
    SHADER_TEXTURE2D_SRV(T14)
    SHADER_TEXTURE2D_SRV(T15)
END_SHADER_PARAMETER_SET()
template<class T>bool FitsBindingCapacity()
{return T::GetShaderParameterMembers().size()<=size_t(jShaderBindingArray::NumOfInlineData);}
template<class T>void RuntimeBindings(std::ostream& out,const char* name,const T& parameters,int expected)
{
    if(!FitsBindingCapacity<T>())throw std::runtime_error("Actual typed parameter metadata exceeds runtime binding capacity");
    jShaderBindingArray bindings;jShaderBindingResourceInlineAllocator resources;
    jShaderParameterSet::BuildShaderBindings(parameters,EShaderAccessStageFlag::COMPUTE,bindings,resources);
    if(bindings.NumOfData!=expected||resources.Offset>=int(sizeof(resources.Data)))
        throw std::runtime_error("Runtime binding/inline resource capacity contract failed");
    out<<std::quoted(name)<<":{\"bindings\":"<<bindings.NumOfData<<",\"capacity\":"<<bindings.NumOfInlineData
       <<",\"resourceBytes\":"<<resources.Offset<<",\"resourceCapacity\":"<<sizeof(resources.Data)<<"}";
}
void CheckRuntimeBindings(const std::filesystem::path& directory)
{
    jTexture texture;const auto uniform=std::make_shared<BindingFixtureUniform>();
    jTextureSubsurfaceFilterParameters filter;filter.TextureSSS.Buffer=uniform;
    filter.TextureSSSInput={&texture};filter.TextureSSSLevel0={&texture};filter.TextureSSSStretch={&texture};
    filter.TextureSSSEntryUV={&texture};filter.TextureSSSFinish={&texture};filter.TextureSSSResult={&texture};filter.TextureSSSSurfaceFlags={&texture};
    jTextureSubsurfaceFinishParameters finish;finish.TextureSSS.Buffer=uniform;
    finish.TextureSSSLevel0={&texture};finish.TextureSSSLevel1={&texture};finish.TextureSSSLevel2={&texture};
    finish.TextureSSSLevel3={&texture};finish.TextureSSSLevel4={&texture};finish.TextureSSSLevel5={&texture};
    finish.TextureSSSStretch={&texture};finish.TextureSSSEntryUV={&texture};finish.TextureSSSFinish={&texture};
    finish.TextureSSSCoverage={&texture};finish.TextureSSSResult={&texture};finish.TextureSSSSurfaceFlags={&texture};
    jTextureSubsurfaceComposeParameters compose;compose.TextureSSS.Buffer=uniform;
    compose.TextureSSSScene={&texture};compose.TextureSSSScreenDiffuse={&texture};compose.TextureSSSAlbedo={&texture};
    compose.TextureSSSMask={&texture};compose.TextureSSSSkinUV={&texture};compose.TextureSSSStretch={&texture};
    compose.TextureSSSEntryUV={&texture};compose.TextureSSSInput={&texture};compose.TextureSSSSurfaceFlags={&texture};
    compose.TextureSSSResult={&texture};compose.TextureSSSCompletedDiffuse={&texture};
    std::ofstream out(directory/"runtime-bindings.json");out<<"{";
    RuntimeBindings(out,"filter",filter,8);out<<",";RuntimeBindings(out,"finish",finish,13);out<<",";
    RuntimeBindings(out,"compose",compose,12);
    if(FitsBindingCapacity<OversizedBindingNegativeControl>())throw std::runtime_error("Oversized runtime binding negative control was accepted");
    out<<",\"negativeControl\":{\"typedParameterCount\":"<<OversizedBindingNegativeControl::GetShaderParameterMembers().size()
       <<",\"rejectedBeforeBuild\":true}}\n";
}
template<class T>void Layout(std::ostream& out,bool& first)
{
    if(!first)out<<",";first=false;
    out<<std::quoted(T::GetShaderUniformBufferTypeName())<<":{\"size\":"<<sizeof(T)<<",\"fields\":{";
    bool f=true;for(const auto& x:T::GetShaderUniformBufferFieldMembers())
    {if(!f)out<<",";f=false;out<<std::quoted(x.Name)<<":"<<x.ByteOffset;}out<<"}}";
}
int main(int argc,char** argv)
{
    if(argc!=3)return 2;
    CheckRuntimeBindings(std::filesystem::path(argv[1]));
    using namespace jEditorTools;
    MaterialGraph graph;
    const auto add=[&](unsigned id,NodeKind kind,unsigned a=0,unsigned b=0)
    {MaterialNode n;n.Id=id;n.Kind=kind;n.InputA=a;n.InputB=b;graph.Nodes.push_back(n);return &graph.Nodes.back();};
    add(1,NodeKind::UV);
    add(2,NodeKind::TextureObject)->Texture="Assets/skin.jpg";
    graph.Nodes.back().Semantic=jMaterialMapping::TextureSemantic::Color;
    add(3,NodeKind::TextureSample,2,1);
    add(4,NodeKind::TextureObject)->Texture="Assets/normal.jpg";
    auto* normal=add(5,NodeKind::TextureSampleNormal,4,1);
    normal->NormalSpace=jMaterialMapping::NormalSpace::Object;normal->InputBasisSignXYZ={1,1,-1};normal->FlipNormalY=false;
    add(6,NodeKind::TextureObject)->Texture="Assets/specAO.png";
    add(7,NodeKind::TextureSample,6,1);
    add(8,NodeKind::Channel,7)->Parameters[0]=0;
    add(9,NodeKind::Channel,7)->Parameters[0]=1;
    add(10,NodeKind::Channel,7)->Parameters[0]=2;
    add(11,NodeKind::Scalar)->Scalar=1.99f;
    add(12,NodeKind::Scalar)->Scalar=.5f;
    add(13,NodeKind::Multiply,8,11);add(14,NodeKind::Multiply,9,12);
    add(15,NodeKind::Scalar)->Scalar=.5f;
    add(16,NodeKind::DiffusionProfile)->Parameters[0]=0;
    graph.BaseColor=3;graph.Normal=5;graph.Roughness=14;graph.SpecularWeight=13;
    graph.DiffuseOcclusion=10;graph.PreScatterWeight=15;graph.SubsurfaceProfile=16;
    jMaterialMapping::Program program;std::string error;
    if(!jMaterialMapping::Compile(graph,program,error))throw std::runtime_error(error);
    const auto sourceRoot=std::filesystem::path(argv[2]);
    for(bool vulkan:{true,false})
    {
        Vulkan=vulkan;const auto target=std::filesystem::path(argv[1])/(vulkan?"vulkan":"dx12");
        std::filesystem::create_directories(target);
        const auto raster=jShaderParameterSet::GenerateHLSL<jTextureSubsurfaceRasterParameters>(0)+
            jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(1)+
            jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(2);
        const auto write=[&](std::string name,std::string declarations,std::string entry,std::string flags,std::string injection="")
        {
            std::ifstream in(sourceRoot/entry);std::string text((std::istreambuf_iterator<char>(in)),{});
            if(!in)throw std::runtime_error("Missing actual shader source");
            if(!injection.empty())
            {
                const std::string marker="// Material shader injection point.";
                const auto p=text.find(marker);if(p==std::string::npos)throw std::runtime_error("Missing shader injection point");
                text.replace(p,marker.size(),injection);
            }
            std::ofstream out(target/(name+".hlsl"));out.exceptions(std::ios::badbit|std::ios::failbit);
            out<<"#include \"common.hlsl\"\n"<<flags<<declarations<<text;
        };
        for(int capture=0;capture<2;++capture)
        {
            const auto prefix=std::string(capture?"capture":"bake");
            const auto flags="#define JE_TEXTURE_SSS_CAPTURE "+std::to_string(capture)+"\n#define USE_ALBEDO_TEXTURE 1\n#define USE_SRGB_ALBEDO_TEXTURE 1\n";
            for(int color=0;color<2;++color)for(int bitangent=0;bitangent<2;++bitangent)
                write(prefix+"-vs-"+std::to_string(color)+std::to_string(bitangent),raster,"texture_subsurface_vs.hlsl",
                    flags+"#define USE_VERTEX_COLOR "+std::to_string(color)+"\n#define USE_VERTEX_BITANGENT "+std::to_string(bitangent)+"\n");
            write(prefix+"-ps",raster,"texture_subsurface_ps.hlsl",flags+"#define PIXEL_SHADER 1\n");
            write(prefix+"-ps-graph",raster,"texture_subsurface_ps.hlsl",flags+"#define PIXEL_SHADER 1\n",program.Hlsl);
        }
        for(int pass=0;pass<7;++pass)
        {
            const auto compute=pass<=4?jShaderParameterSet::GenerateHLSL<jTextureSubsurfaceFilterParameters>(0):
                pass==5?jShaderParameterSet::GenerateHLSL<jTextureSubsurfaceFinishParameters>(0):
                    jShaderParameterSet::GenerateHLSL<jTextureSubsurfaceComposeParameters>(0);
            write("compute-"+std::to_string(pass),compute,"texture_subsurface_cs.hlsl","#define JE_TEXTURE_SSS_PASS "+std::to_string(pass)+"\n");
        }
        std::ofstream out(target/"cpu-layout.json");out<<"{";bool first=true;
        Layout<jTextureSubsurfaceData>(out,first);Layout<RenderObjectUniformBuffer>(out,first);
        Layout<jMaterialGraphUniformBuffer>(out,first);out<<"}\n";
    }
}
'''


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def atomic_format(path):
    data = path.read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    cursor, formats, capabilities, atomic_or = 5, [], [], 0
    while cursor < len(words):
        count, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
        if not count or cursor + count > len(words):
            raise ValueError("Invalid SPIR-V instruction")
        op = words[cursor + 1:cursor + count]
        if opcode == 17:
            capabilities.append(op[0])
        if opcode == 25 and len(op) >= 8 and op[6] == 2:
            formats.append(op[7])
        atomic_or += opcode == 241
        cursor += count
    if formats != [33] or not atomic_or or 55 in capabilities or 56 in capabilities:
        raise ValueError(f"Surface flags require explicit R32ui atomic OR without formatless features: {formats}, {atomic_or}, {capabilities}")
    return {"storage_format": "R32ui", "atomic_or": atomic_or, "formatless_features": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = (args.output or root / "build/texture-subsurface/shaders").resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands = args.compile_commands or root / "build/macos-debug/compile_commands.json"
    dxc = args.dxc or root / ".ci-tools/dxc/bin/dxc"
    helpers = module("tsss_export", root / "scripts/ci/check-advanced-shadow-shaders.py")
    checks = module("tsss_layout", root / "scripts/ci/check-shaders.py")
    report = {"success": False, "scope": "actual C++ declaration/material graph exporter and HLSL compilation; no GPU", "shaders": []}
    try:
        source, exporter = output / "export.cpp", output / "export-shaders"
        source.write_text(EXPORTER)
        helpers.run(helpers.exporter_command(commands, source, exporter), output / "export-build.log", root)
        helpers.run([exporter, output, root / "jEngine/Resource/Shaders/hlsl"], output / "export.log", root)
        report["actual_runtime_bindings"] = json.loads((output / "runtime-bindings.json").read_text())
        reflected = set()
        for api in ("vulkan", "dx12"):
            target = output / api
            contracts = json.loads((target / "cpu-layout.json").read_text())
            for shader in sorted(target.glob("*.hlsl")):
                if shader.name.endswith(".flattened.hlsl"):
                    continue
                name = shader.stem
                flattened = target / (name + ".flattened.hlsl")
                flattened.write_text(checks.flatten_includes(shader.read_text(), root / "jEngine/Resource/Shaders/hlsl", set()))
                profile = "vs_6_6" if "-vs-" in name else "ps_6_6" if "-ps" in name else "cs_6_6"
                binary = target / (name + (".spv" if api == "vulkan" else ".dxil"))
                command = [dxc, "-T", profile, "-E", "main", "-Fo", binary, flattened]
                if api == "vulkan":
                    command += ["-spirv", "-fspv-target-env=vulkan1.1spirv1.4", "-fvk-use-scalar-layout", "-fspv-use-unknown-image-format"]
                helpers.run(command, target / (name + ".log"), root)
                if api == "vulkan":
                    reflected.update(checks.check_layout(binary, contracts))
                report["shaders"].append(api + "/" + name)
        if set(contracts) != reflected:
            raise ValueError(f"Unreflected actual uniform types: {set(contracts) - reflected}")
        report["surface_flags"] = atomic_format(output / "vulkan/compute-1.spv")
        negative = json.loads((output / "vulkan/cpu-layout.json").read_text())
        negative["jTextureSubsurfaceData"]["fields"]["Filter"] += 4
        try:
            checks.check_layout(output / "vulkan/compute-2.spv", negative)
        except ValueError as expected:
            report["negative_layout_control"] = str(expected)
        else:
            raise ValueError("Incorrect Filter offset was accepted")
        report.update(success=True, layout_types=sorted(reflected))
        print(f"PASS: {len(report['shaders'])} UV/TSM DXIL/SPIR-V shaders, actual material graph, 464B transport ABI, typed R32ui atomic flags")
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
