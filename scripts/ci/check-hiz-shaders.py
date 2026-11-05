#!/usr/bin/env python3
"""Compile real Hi-Z declarations/shaders and verify packed GPU ABI; no engine/GPU build."""
import argparse
import importlib.util
import json
from pathlib import Path

EXPORTER = r'''
#define XXH_INLINE_ALL
#include "pch.h"
#include "Shader/jHiZOcclusionShaderParameters.h"
#include <iomanip>
static bool UseVulkan=true;
bool IsUseVulkan(){return UseVulkan;}
bool IsUseDX12(){return !UseVulkan;}
template<class T>void Layout(std::ostream& file,bool& first)
{
    if(!first)file<<",";first=false;
    file<<std::quoted(T::GetShaderUniformBufferTypeName())<<":{\"size\":"<<sizeof(T)<<",\"fields\":{";
    bool firstField=true;
    for(const auto& field:T::GetShaderUniformBufferFieldMembers())
    {if(!firstField)file<<",";firstField=false;file<<std::quoted(field.Name)<<":"<<field.ByteOffset;}
    file<<"}}";
}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    for(bool vulkan:{true,false})
    {
        UseVulkan=vulkan;const auto target=std::filesystem::path(argv[1])/(vulkan?"vulkan":"dx12");
        std::filesystem::create_directories(target);
        auto write=[&](const char* name,const std::string& declarations,const char* entry,const std::string& defines="")
        {
            std::ofstream file(target/(std::string(name)+".hlsl"));file.exceptions(std::ios::badbit|std::ios::failbit);
            file<<"#include \"common.hlsl\"\n"<<defines<<declarations<<"\n#include \""<<entry<<"\"\n";
        };
        const auto depth=jShaderParameterSet::GenerateHLSL<jHiZDepthParameters>(0);
        write("hiz-depth-vs",depth,"hiz_occlusion_depth_vs.hlsl");
        write("hiz-depth-ps",depth,"hiz_occlusion_depth_ps.hlsl");
        write("hiz-seed",jShaderParameterSet::GenerateHLSL<jHiZSeedParameters>(0),"hiz_occlusion_cs.hlsl","#define JE_HIZ_PASS 0\n");
        write("hiz-reduce",jShaderParameterSet::GenerateHLSL<jHiZReduceParameters>(0),"hiz_occlusion_cs.hlsl","#define JE_HIZ_PASS 1\n");
        write("hiz-cull",jShaderParameterSet::GenerateHLSL<jHiZCullParameters>(0),"hiz_occlusion_cs.hlsl","#define JE_HIZ_PASS 2\n");
        std::ofstream file(target/"cpu-layout.json");bool first=true;file<<"{";
        Layout<jHiZDepthData>(file,first);Layout<jHiZDispatchData>(file,first);Layout<jHiZCandidate>(file,first);
        file<<",\"jHiZIndexedArguments\":{\"size\":"<<sizeof(jHiZIndexedArguments)<<",\"fields\":{"
            <<"\"IndexCount\":"<<offsetof(jHiZIndexedArguments,IndexCount)<<",\"InstanceCount\":"<<offsetof(jHiZIndexedArguments,InstanceCount)
            <<",\"FirstIndex\":"<<offsetof(jHiZIndexedArguments,FirstIndex)<<",\"BaseVertex\":"<<offsetof(jHiZIndexedArguments,BaseVertex)
            <<",\"FirstInstance\":"<<offsetof(jHiZIndexedArguments,FirstInstance)<<"}}}\n";
    }
}
'''


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = (args.output or root / "build/hiz-occlusion/shaders").resolve()
    commands = (args.compile_commands or root / "build/macos-debug/compile_commands.json").resolve()
    dxc = (args.dxc or root / ".ci-tools/dxc/bin/dxc").resolve()
    output.mkdir(parents=True, exist_ok=True)
    helpers = load("hiz_export_helpers", root / "scripts/ci/check-advanced-shadow-shaders.py")
    checks = load("hiz_shader_layout", root / "scripts/ci/check-shaders.py")
    report = {"success": False, "scope": "actual generated C++ declarations and DXIL/SPIR-V compilation; no GPU", "shaders": []}
    try:
        source = output / "export.cpp"
        source.write_text(EXPORTER, encoding="utf-8")
        exporter = output / "export-shaders"
        helpers.run(helpers.exporter_command(commands, source, exporter), output / "export-build.log", root)
        helpers.run([exporter, output], output / "export.log", root)
        reflected = set()
        for api in ("vulkan", "dx12"):
            target = output / api
            contracts = json.loads((target / "cpu-layout.json").read_text())
            for name, profile in (("hiz-depth-vs", "vs_6_6"), ("hiz-depth-ps", "ps_6_6"),
                                  ("hiz-seed", "cs_6_6"), ("hiz-reduce", "cs_6_6"), ("hiz-cull", "cs_6_6")):
                flattened = target / (name + ".flattened.hlsl")
                flattened.write_text(checks.flatten_includes((target / (name + ".hlsl")).read_text(), root / "jEngine/Resource/Shaders/hlsl", set()))
                binary = target / (name + (".spv" if api == "vulkan" else ".dxil"))
                command = [dxc, "-T", profile, "-E", "main", "-Fo", binary, flattened]
                if api == "vulkan":
                    command += ["-spirv", "-fspv-target-env=vulkan1.1spirv1.4", "-fvk-use-scalar-layout", "-fspv-use-unknown-image-format"]
                helpers.run(command, target / (name + ".log"), root)
                if api == "vulkan":
                    reflected.update(checks.check_layout(binary, contracts))
                report["shaders"].append(api + "/" + name)
        if set(contracts) - reflected:
            raise ValueError("Unreflected Hi-Z CPU layouts: " + str(sorted(set(contracts) - reflected)))
        negative = json.loads((output / "vulkan/cpu-layout.json").read_text())
        negative["jHiZIndexedArguments"]["size"] = 32
        try:
            checks.check_layout(output / "vulkan/hiz-cull.spv", negative)
        except ValueError as expected:
            report["negative_stride_control"] = str(expected)
        else:
            raise ValueError("Incorrect 32-byte indirect argument stride was accepted")
        report.update(success=True, layout_types=sorted(reflected))
        print(f"PASS: {len(report['shaders'])} DXIL/SPIR-V compilations; {len(reflected)} actual CPU layout contracts; negative 32-byte stride rejected")
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
