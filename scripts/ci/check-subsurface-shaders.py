#!/usr/bin/env python3
"""Compile production SSS declarations/HLSL for DXIL and SPIR-V, without a GPU.

Only a small declaration exporter is compiled using existing no-PCH platform
flags. This does not build the engine, fetch dependencies or run rendering.
"""
import argparse
import importlib.util
import json
from pathlib import Path


EXPORTER = r'''
#define XXH_INLINE_ALL
#include "pch.h"
#include "Shader/jSubsurfaceShaderParameters.h"
#include <iomanip>

static bool UseVulkan = true;
bool IsUseVulkan() { return UseVulkan; }
bool IsUseDX12() { return !UseVulkan; }

template<typename T>
void Layout(std::ostream& file, bool& first)
{
    if (!first) file << ",";
    first = false;
    file << std::quoted(T::GetShaderUniformBufferTypeName()) << ":{\"size\":"
         << sizeof(T) << ",\"fields\":{";
    bool firstField = true;
    for (const auto& field : T::GetShaderUniformBufferFieldMembers())
    {
        if (!firstField) file << ",";
        firstField = false;
        file << std::quoted(field.Name) << ":" << field.ByteOffset;
    }
    file << "}}";
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    for (bool vulkan : {true, false})
    {
        UseVulkan = vulkan;
        const auto target = std::filesystem::path(argv[1]) / (vulkan ? "vulkan" : "dx12");
        std::filesystem::create_directories(target);
        const auto declarations = jShaderParameterSet::GenerateHLSL<jSubsurfaceComputeParameters>(0);
        for (int pass = 0; pass < 3; ++pass)
        {
            std::ofstream file(target / ("subsurface-" + std::to_string(pass) + ".hlsl"));
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file << "#include \"common.hlsl\"\n#define JE_SUBSURFACE_PASS " << pass << '\n'
                 << declarations << "\n#include \"subsurface_cs.hlsl\"\n";
        }
        std::ofstream layout(target / "cpu-layout.json");
        layout.exceptions(std::ios::badbit | std::ios::failbit);
        layout << "{";
        bool first = true;
        Layout<jSubsurfaceViewData>(layout, first);
        Layout<jSubsurfaceProfileData>(layout, first);
        layout << "}\n";
    }
}
'''


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = (args.output or root / "build/subsurface/shader-checks").resolve()
    commands = (args.compile_commands or root / "build/macos-debug/compile_commands.json").resolve()
    dxc = (args.dxc or root / ".ci-tools/dxc/bin/dxc").resolve()
    output.mkdir(parents=True, exist_ok=True)
    driver_tools = module(root / "scripts/ci/check-advanced-shadow-shaders.py", "sss_export_tools")
    checks = module(root / "scripts/ci/check-shaders.py", "sss_shader_checks")
    report = {"success": False, "scope": "production declaration exporter and HLSL compilation; no GPU",
              "shaders": [], "layout_types": []}
    try:
        driver = output / "export.cpp"
        driver.write_text(EXPORTER)
        exporter = output / "export-shaders"
        driver_tools.run(driver_tools.exporter_command(commands, driver, exporter),
                         output / "exporter-build.log", root)
        driver_tools.run([exporter, output], output / "export.log", root)
        reflected = set()
        for api in ("vulkan", "dx12"):
            target = output / api
            contracts = json.loads((target / "cpu-layout.json").read_text())
            for shader_pass in range(3):
                name = f"subsurface-{shader_pass}"
                flattened = target / (name + ".flattened.hlsl")
                flattened.write_text(checks.flatten_includes((target / (name + ".hlsl")).read_text(),
                    root / "jEngine/Resource/Shaders/hlsl", set()))
                binary = target / (name + (".spv" if api == "vulkan" else ".dxil"))
                command = [dxc, "-T", "cs_6_0", "-E", "main", "-Fo", binary, flattened]
                if api == "vulkan":
                    command += ["-spirv", "-fspv-target-env=vulkan1.1spirv1.4", "-fvk-use-scalar-layout",
                                "-fspv-use-unknown-image-format"]
                driver_tools.run(command, target / (name + ".log"), root)
                if api == "vulkan":
                    reflected.update(checks.check_layout(binary, contracts))
                report["shaders"].append(f"{api}/{name}")
        if set(contracts) - reflected:
            raise ValueError(f"Unreflected SSS layouts: {sorted(set(contracts) - reflected)}")
        negative = json.loads((output / "vulkan/cpu-layout.json").read_text())
        negative["jSubsurfaceViewData"]["fields"]["FilterParams"] += 4
        try:
            checks.check_layout(output / "vulkan/subsurface-0.spv", negative)
        except ValueError as expected:
            report["negative_layout_control"] = str(expected)
        else:
            raise ValueError("Incorrect SSS FilterParams offset was accepted")
        report.update(success=True, layout_types=sorted(reflected))
        print(f"PASS: {len(report['shaders'])} SSS DXIL/SPIR-V compilations; {len(reflected)} CPU layout contracts")
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
