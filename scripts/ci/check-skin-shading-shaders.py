#!/usr/bin/env python3
"""Compile real skin material/LUT declarations to DXIL/SPIR-V, without an engine/GPU build."""
import argparse
import importlib.util
import json
import struct
from pathlib import Path

EXPORTER = r'''
#define XXH_INLINE_ALL
#include "pch.h"
#include "Shader/jSkinShadingShaderParameters.h"
#include <iomanip>
static bool Vulkan=true;
bool IsUseVulkan(){return Vulkan;}
bool IsUseDX12(){return !Vulkan;}
BEGIN_SHADER_PARAMETER_SET(SkinReaderOutputParameters)
    SHADER_RW_TEXTURE2D(SkinReadback)
END_SHADER_PARAMETER_SET()
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    for(bool vulkan:{true,false})
    {
        Vulkan=vulkan;const auto target=std::filesystem::path(argv[1])/(vulkan?"vulkan":"dx12");
        std::filesystem::create_directories(target);
        const auto write=[&](const char* name,const std::string& code)
        {std::ofstream file(target/name);file.exceptions(std::ios::badbit|std::ios::failbit);file<<code;};
        write("skin-energy.hlsl",std::string("#include \"common.hlsl\"\n")+
            jShaderParameterSet::GenerateHLSL<jSkinEnergyBuildParameters>(0)+"\n#include \"skin_energy_cs.hlsl\"\n");
        write("skin-reader.hlsl",std::string("#include \"common.hlsl\"\n")+
            jShaderParameterSet::GenerateHLSL<jSkinEnergyShaderParameters>(4)+
            jShaderParameterSet::GenerateHLSL<SkinReaderOutputParameters>(5)+R"(
#include "Features/skinshading.hlsl"
[numthreads(8,8,1)]void main(uint3 thread:SV_DispatchThreadID)
{
    const float2 uv=(float2(thread.xy)+0.5)/64.0;
    const float energy=JE_SkinEnergyLookup(SkinEnergyLUT,SkinEnergyLUTSampler,uv.x,uv.y);
    const float spec=JE_SkinKelemenSpecular(float3(0,0,1),normalize(float3(uv,1)),float3(0,0,1),uv.y,1.99);
    SkinReadback[thread.xy]=float4(energy,JE_SkinDiffuseEnergy(1.99,energy),spec,1);
}
)");
        std::ofstream layout(target/"cpu-layout.json");
        layout<<"{\"jSkinEnergyBuildData\":{\"size\":"<<sizeof(jSkinEnergyBuildData)<<",\"fields\":{";
        bool first=true;
        for(const auto& field:jSkinEnergyBuildData::GetShaderUniformBufferFieldMembers())
        {if(!first)layout<<",";first=false;layout<<std::quoted(field.Name)<<":"<<field.ByteOffset;}
        layout<<"}}}\n";
    }
}
'''


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def check_manual_lookup(path):
    data = path.read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError("Invalid SPIR-V header")
    cursor, fetches, filtered = 5, 0, 0
    while cursor < len(words):
        length, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
        if length == 0 or cursor + length > len(words):
            raise ValueError("Invalid SPIR-V instruction")
        fetches += opcode == 95  # OpImageFetch, bundled Khronos SPIRV-Headers.
        filtered += 87 <= opcode <= 94  # OpImageSample* variants.
        cursor += length
    if fetches != 4 or filtered != 0:
        raise ValueError(f"Expected four Load taps and zero filtered image samples, got {fetches}/{filtered}")
    return {"integer_load_taps": fetches, "filtered_image_samples": filtered}


def check_r32f_storage(path):
    data = path.read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError("Invalid SPIR-V header")
    cursor, storage_formats, capabilities = 5, [], []
    while cursor < len(words):
        length, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
        if length == 0 or cursor + length > len(words):
            raise ValueError("Invalid SPIR-V instruction")
        operands = words[cursor + 1:cursor + length]
        if opcode == 17:  # OpCapability
            capabilities.append(operands[0])
        if opcode == 25 and len(operands) >= 8 and operands[6] == 2:  # OpTypeImage, Sampled=2(storage)
            storage_formats.append(operands[7])
        cursor += length
    if storage_formats != [3] or 56 in capabilities:  # R32f=3, StorageImageWriteWithoutFormat=56.
        raise ValueError(f"Expected explicit R32f storage and no formatless writes, got formats={storage_formats}, capabilities={capabilities}")
    return {"storage_image_formats": ["R32f"], "storage_image_write_without_format": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = (args.output or root / "build/skin-shading/shaders").resolve()
    commands = (args.compile_commands or root / "build/macos-debug/compile_commands.json").resolve()
    dxc = (args.dxc or root / ".ci-tools/dxc/bin/dxc").resolve()
    output.mkdir(parents=True, exist_ok=True)
    helpers = module("skin_export", root / "scripts/ci/check-advanced-shadow-shaders.py")
    checks = module("skin_layout", root / "scripts/ci/check-shaders.py")
    report = {"success": False, "scope": "actual C++ declarations and DXIL/SPIR-V compilation; no GPU", "shaders": []}
    try:
        source, exporter = output / "export.cpp", output / "export-shaders"
        source.write_text(EXPORTER, encoding="utf-8")
        helpers.run(helpers.exporter_command(commands, source, exporter), output / "export-build.log", root)
        helpers.run([exporter, output], output / "export.log", root)
        reflected = set()
        for api in ("vulkan", "dx12"):
            target = output / api
            contracts = json.loads((target / "cpu-layout.json").read_text())
            for name in ("skin-energy", "skin-reader"):
                shader = target / (name + ".flattened.hlsl")
                shader.write_text(checks.flatten_includes((target / (name + ".hlsl")).read_text(),
                    root / "jEngine/Resource/Shaders/hlsl", set()), encoding="utf-8")
                binary = target / (name + (".spv" if api == "vulkan" else ".dxil"))
                command = [dxc, "-T", "cs_6_6", "-E", "main", "-Fo", binary, shader]
                if api == "vulkan":
                    command += ["-spirv", "-fspv-target-env=vulkan1.1spirv1.4", "-fvk-use-scalar-layout", "-fspv-use-unknown-image-format"]
                helpers.run(command, target / (name + ".log"), root)
                if api == "vulkan" and name == "skin-energy":
                    reflected.update(checks.check_layout(binary, contracts))
                report["shaders"].append(api + "/" + name)
        if set(contracts) != reflected:
            raise ValueError("Skin energy uniform was not reflected from the actual shader")
        report["manual_lookup"] = check_manual_lookup(output / "vulkan/skin-reader.spv")
        report["lut_storage"] = check_r32f_storage(output / "vulkan/skin-energy.spv")
        format_control = output / "vulkan/skin-energy-formatless-control.hlsl"
        original = (output / "vulkan/skin-energy.flattened.hlsl").read_text()
        annotation = '[[vk::image_format("r32f")]]'
        if original.count(annotation) != 1:
            raise ValueError("The actual typed skin LUT declaration did not contain its one R32f annotation")
        format_control.write_text(original.replace(annotation, ""), encoding="utf-8")
        control_binary = output / "vulkan/skin-energy-formatless-control.spv"
        helpers.run([dxc, "-T", "cs_6_6", "-E", "main", "-Fo", control_binary,
            "-spirv", "-fspv-target-env=vulkan1.1spirv1.4", "-fvk-use-scalar-layout", "-fspv-use-unknown-image-format", format_control],
            output / "vulkan/skin-energy-formatless-control.log", root)
        try:
            check_r32f_storage(control_binary)
        except ValueError as expected:
            report["negative_storage_format_control"] = str(expected)
        else:
            raise ValueError("Formatless LUT storage was incorrectly accepted")
        negative = json.loads((output / "vulkan/cpu-layout.json").read_text())
        negative["jSkinEnergyBuildData"]["fields"]["DimensionsAndRoughness"] = 4
        try:
            checks.check_layout(output / "vulkan/skin-energy.spv", negative)
        except ValueError as expected:
            report["negative_layout_control"] = str(expected)
        else:
            raise ValueError("An incorrect skin uniform offset was accepted")
        report.update(success=True, layout_types=sorted(reflected))
        print("PASS: 4 DXIL/SPIR-V shaders; 16-byte uniform; four Load taps; explicit R32f/no formatless writes; ABI/storage negative controls")
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
