#!/usr/bin/env python3
"""Check advanced-shadow HLSL with actual generated C++ declarations, without a GPU.

Only the small exporter translation unit is built. Existing CMake compile
commands supply platform/include flags; no engine build or dependency setup runs.
"""
import argparse
import importlib.util
import json
import shlex
import struct
import subprocess
from pathlib import Path


EXPORTER = r'''
#define XXH_INLINE_ALL
#include "pch.h"
#include "Shader/jAdvancedShadowShaderParameters.h"
#include "Shader/jLightingShaderParameters.h"
#include <iomanip>

static bool UseVulkan = true;
bool IsUseVulkan() { return UseVulkan; }
bool IsUseDX12() { return !UseVulkan; }

BEGIN_SHADER_PARAMETER_SET(AdvancedShadowReaderFixtureParameters)
    SHADER_UNIFORM_BUFFER(jAdvancedShadowData, AdvancedShadow)
    SHADER_TEXTURE2D(AdvancedShadowMap)
    SHADER_TEXTURE2D_SRV(DeepShadow0)
    SHADER_TEXTURE2D_SRV(DeepShadow1)
    SHADER_RW_TEXTURE2D(ShadowResult)
END_SHADER_PARAMETER_SET()

template <typename T>
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
        const std::string raster = jShaderParameterSet::GenerateHLSL<jAdvancedShadowRasterParameters>(0);
        const std::string material = jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(1)
            + jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(2);
        const std::string compute = jShaderParameterSet::GenerateHLSL<jAdvancedShadowComputeParameters>(0);
        auto write = [&](const char* name, const std::string& declarations, const std::string& entry,
                         const std::string& defines = "")
        {
            std::ofstream file(target / (std::string(name) + ".hlsl"));
            file.exceptions(std::ios::badbit | std::ios::failbit);
            // Same ordering as runtime injection: common types, generated
            // declarations, then helper/function bodies from the root entry.
            file << "#include \"common.hlsl\"\n" << defines << declarations << entry;
        };
        write("shadow-vs", raster + material, "\n#include \"advanced_shadow_vs.hlsl\"\n");
        write("shadow-volume-vs", raster, "\n#include \"advanced_shadow_vs.hlsl\"\n",
              "#define JE_ADVANCED_SHADOW_VOLUME 1\n");
        write("shadow-ps", raster, "\n#include \"advanced_shadow_fs.hlsl\"\n");
        write("shadow-volume-ps", raster, "\n#include \"advanced_shadow_fs.hlsl\"\n",
              "#define JE_ADVANCED_SHADOW_VOLUME 1\n");
        write("shadow-deep-ps", raster, "\n#include \"advanced_shadow_fs.hlsl\"\n",
              "#define JE_ADVANCED_SHADOW_DEEP 1\n");
        for (int pass = 0; pass < 3; ++pass)
        {
            const std::string name = std::string("shadow-compute-") + std::to_string(pass);
            write(name.c_str(), compute, "\n#include \"advanced_shadow_cs.hlsl\"\n",
                  "#define JE_ADVANCED_SHADOW_PASS " + std::to_string(pass) + "\n");
        }
        write("shadow-volume-mask-vs", "", "\n#include \"advanced_shadow_volume_mask.hlsl\"\n");
        write("shadow-volume-mask-ps", "", "\n#include \"advanced_shadow_volume_mask.hlsl\"\n");
        const std::string view = jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0);
        const std::string lightFlags = "#define COMPUTE_SHADER 0\n#define PIXEL_SHADER 1\n"
            "#define USE_PBR 1\n#define USE_VARIABLE_SHADING_RATE 0\n#define USE_SHADOW_MAP 1\n";
        for (bool subpass : {false, true})
        {
            if (subpass && !vulkan) continue;
            const std::string suffix = subpass ? "-subpass" : "";
            const std::string flags = lightFlags + "#define USE_SUBPASS " + (subpass ? "1\n" : "0\n")
                + "#define USE_REVERSEZ 0\n";
            const std::string scene = subpass
                ? jShaderParameterSet::GenerateHLSL<jSceneSubpassInputShaderParameters>(3)
                : jShaderParameterSet::GenerateHLSL<jSceneTexturesShaderParameters>(3);
            write(("light-directional" + suffix).c_str(), view
                + jShaderParameterSet::GenerateHLSL<jDirectionalLightShaderParameters>(1)
                + jShaderParameterSet::GenerateHLSL<jDirectionalLightIBLShaderParameters>(2) + scene,
                "\n#include \"directionallight_fs.hlsl\"\n", flags);
            write(("light-point" + suffix).c_str(), view
                + jShaderParameterSet::GenerateHLSL<jPointLightShaderParameters>(1)
                + jShaderParameterSet::GenerateHLSL<jLightVolumeVertexShaderParameters>(2) + scene,
                "\n#include \"pointlight_fs.hlsl\"\n", flags);
            write(("light-spot" + suffix).c_str(), view
                + jShaderParameterSet::GenerateHLSL<jSpotLightShaderParameters>(1)
                + jShaderParameterSet::GenerateHLSL<jLightVolumeVertexShaderParameters>(2) + scene,
                "\n#include \"spotlight_fs.hlsl\"\n", flags);
        }
        const std::string forward = view
            + jShaderParameterSet::GenerateHLSL<jDirectionalLightShaderParameters>(1)
            + jShaderParameterSet::GenerateHLSL<jPointLightShaderParameters>(2)
            + jShaderParameterSet::GenerateHLSL<jSpotLightShaderParameters>(3)
            + jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(4)
            + jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(5);
        for (int reverseZ = 0; reverseZ <= 1; ++reverseZ)
            write((std::string("light-forward-") + std::to_string(reverseZ)).c_str(), forward,
                "\n#include \"shader_fs.hlsl\"\n", lightFlags
                    + "#define USE_REVERSEZ " + std::to_string(reverseZ) + "\n");
        write("light-spot-reverse", view
            + jShaderParameterSet::GenerateHLSL<jSpotLightShaderParameters>(1)
            + jShaderParameterSet::GenerateHLSL<jLightVolumeVertexShaderParameters>(2)
            + jShaderParameterSet::GenerateHLSL<jSceneTexturesShaderParameters>(3),
            "\n#include \"spotlight_fs.hlsl\"\n", lightFlags
                + "#define USE_SUBPASS 0\n#define USE_REVERSEZ 1\n");
        write("shadow-reader", jShaderParameterSet::GenerateHLSL<AdvancedShadowReaderFixtureParameters>(0), R"HLSL(
#include "Features/advancedshadows.hlsl"
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= (uint2)AdvancedShadow.MapSizeAndInvSize.xy)) return;
    const float3 world = float3(float2(id.xy) * AdvancedShadow.MapSizeAndInvSize.zw, 0.5);
    const uint representation = (uint)AdvancedShadow.ModeParams.x;
    const uint projection = (uint)AdvancedShadow.ModeParams.y;
    const float viewDepth = dot(world - AdvancedShadow.CameraPositionAndType.xyz,
        AdvancedShadow.CameraForwardAndOpacity.xyz);
    uint face = 0;
    if (projection == JE_SHADOW_PROJECTION_CSM)
        face = JE_SelectShadowCascade(AdvancedShadow.CascadeSplits,
            (uint)AdvancedShadow.ModeParams.z, viewDepth);
    else if (projection == JE_SHADOW_PROJECTION_CUBE_ATLAS)
        face = JE_ShadowCubeFace(world - AdvancedShadow.LightPositionAndNear.xyz);
    float3 uvDepth;
    bool inside = JE_ProjectShadow(AdvancedShadow.ShadowVP[face], world, uvDepth);
    if (projection == JE_SHADOW_PROJECTION_DUAL)
    {
        const float3 relative = world - AdvancedShadow.LightPositionAndNear.xyz;
        face = relative.z >= 0.0 ? 0u : 1u;
        const float4 clipPosition = JE_DualParaboloidClipPosition(relative, face,
            AdvancedShadow.LightPositionAndNear.w, AdvancedShadow.FarAndDeepParams.x,
            AdvancedShadow.AtlasParams.w);
        uvDepth = float3(clipPosition.x * 0.5 + 0.5, 0.5 - clipPosition.y * 0.5, clipPosition.z);
        inside = true;
    }
    uvDepth.z = JE_ShadowMomentDepth(uvDepth.z, representation, projection,
        (uint)AdvancedShadow.CameraPositionAndType.w, AdvancedShadow.LightPositionAndNear.w, AdvancedShadow.FarAndDeepParams.x);
    float visibility = inside ? JE_SampleAdvancedShadowRect(AdvancedShadowMap, DeepShadow0, DeepShadow1,
        AdvancedShadowMapSampler, uvDepth, JE_ShadowAtlasRect(face, uint2(AdvancedShadow.AtlasParams.xy)),
        representation, AdvancedShadow.FarAndDeepParams.y, AdvancedShadow.FarAndDeepParams.z,
        AdvancedShadow.FilterParams.x, AdvancedShadow.FilterParams.y, AdvancedShadow.FilterParams.z,
        AdvancedShadow.SamplingParams.y, (int)AdvancedShadow.SamplingParams.x, AdvancedShadow.FarAndDeepParams.w) : 1.0;
    if (representation == JE_SHADOW_DEPTH && projection == JE_SHADOW_PROJECTION_CSM)
        visibility = JE_SampleCascadeShadow(AdvancedShadowMap, world, viewDepth,
            AdvancedShadow.ShadowVP, AdvancedShadow.CascadeSplits, (uint)AdvancedShadow.ModeParams.z,
            AdvancedShadow.FilterParams.z, AdvancedShadow.SamplingParams.y,
            (int)AdvancedShadow.SamplingParams.x, 0.05);
    if (representation == JE_SHADOW_DEPTH && projection == JE_SHADOW_PROJECTION_DUAL)
        visibility = JE_SampleDualParaboloidShadow(AdvancedShadowMap, world, AdvancedShadow.LightPositionAndNear.xyz,
            AdvancedShadow.LightPositionAndNear.w, AdvancedShadow.FarAndDeepParams.x, AdvancedShadow.AtlasParams.w,
            AdvancedShadow.FilterParams.z, AdvancedShadow.SamplingParams.y, (int)AdvancedShadow.SamplingParams.x);
    ShadowResult[id.xy] = visibility.xxxx;
}
)HLSL");
        std::ofstream layout(target / "cpu-layout.json");
        layout.exceptions(std::ios::badbit | std::ios::failbit);
        bool first = true;
        layout << "{";
        Layout<jAdvancedShadowData>(layout, first);
        Layout<RenderObjectUniformBuffer>(layout, first);
        Layout<jMaterialGraphUniformBuffer>(layout, first);
        Layout<ViewUniformBuffer>(layout, first);
        layout << "}\n";
    }
}
'''

SHADERS = {
    "shadow-vs": ("main", "vs_6_6"),
    "shadow-volume-vs": ("main", "vs_6_6"),
    "shadow-ps": ("main", "ps_6_6"),
    "shadow-volume-ps": ("main", "ps_6_6"),
    "shadow-deep-ps": ("main", "ps_6_6"),
    **{f"shadow-compute-{p}": ("main", "cs_6_6") for p in range(3)},
    "shadow-volume-mask-vs": ("VSMain", "vs_6_6"),
    "shadow-volume-mask-ps": ("PSMain", "ps_6_6"),
    "shadow-reader": ("main", "cs_6_6"),
    **{f"light-{kind}{suffix}": ("main", "ps_6_6")
       for kind in ("directional", "point", "spot") for suffix in ("", "-subpass")},
    "light-spot-reverse": ("main", "ps_6_6"),
    "light-forward-0": ("main", "ps_6_6"),
    "light-forward-1": ("main", "ps_6_6"),
}


def run(command, output, cwd):
    result = subprocess.run([str(v) for v in command], cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output.write_text(result.stdout)
    if result.returncode:
        tail = "\n".join(result.stdout.splitlines()[-40:])
        raise RuntimeError(f"{output.name}: command failed ({result.returncode}); full log: {output}\n{tail}")


def exporter_command(commands_path, source, output):
    commands = json.loads(commands_path.read_text())
    command = next(item for item in commands if item["file"].endswith("/jEngine/Main.cpp"))
    tokens = command.get("arguments") or shlex.split(command["command"])
    flags, index = [], 1
    while index < len(tokens):
        token = tokens[index]
        if token in ("-o", "-c"):
            index += 2
        elif token == "-Xclang" and tokens[index + 1] in ("-include-pch", "-include"):
            index += 4
        elif token in ("-g", "-Winvalid-pch") or token.startswith("-O"):
            index += 1
        else:
            flags.append(token)
            index += 1
    return [tokens[0], *flags, "-O2", "-Wno-deprecated-builtins", "-Wno-macro-redefined",
            source, "-Wl,-dead_strip", "-o", output]


def check_volume_fixed_depth(path):
    # Values from the bundled Khronos SPIRV-Headers, unified1/spirv.hpp.
    data = path.read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError(f"Not SPIR-V: {path}")
    position = 5
    while position < len(words):
        count, opcode = words[position] >> 16, words[position] & 0xFFFF
        if not count or position + count > len(words):
            raise ValueError("Invalid volume shader SPIR-V instruction")
        operands = words[position + 1:position + count]
        if opcode == 252:  # OpKill
            raise ValueError("Volume fragment shader discards closed-cap fragments")
        if opcode == 16 and len(operands) >= 2 and operands[1] == 12:  # DepthReplacing
            raise ValueError("Volume fragment shader replaces fixed-function depth")
        if opcode == 71 and len(operands) >= 3 and operands[1:3] == (11, 22):
            raise ValueError("Volume fragment shader declares BuiltIn FragDepth")
        position += count
    return {"noDiscard": True, "noDepthReplacement": True, "noFragDepthOutput": True}


def check_point_moment_encoding(path):
    # A conversion texel reads only its own nearest depth. A second-moment
    # slope term derived from neighboring depth texels is not a raster derivative.
    data = path.read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError(f"Not SPIR-V: {path}")
    position, fetches = 5, 0
    while position < len(words):
        count, opcode = words[position] >> 16, words[position] & 0xFFFF
        if not count or position + count > len(words):
            raise ValueError("Invalid point moment encoding SPIR-V instruction")
        fetches += opcode == 95  # OpImageFetch, bundled Khronos SPIRV-Headers.
        position += count
    if fetches != 1:
        raise ValueError(f"Point moment encoding must fetch only its center depth, found {fetches} image fetches")
    return {"nearestDepthImageFetches": fetches}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = (args.output or root / "build/advanced-shadows/shader-checks").resolve()
    commands = (args.compile_commands or root / "build/macos-debug/compile_commands.json").resolve()
    dxc = (args.dxc or root / ".ci-tools/dxc/bin/dxc").resolve()
    output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("engine_shader_checks", root / "scripts/ci/check-shaders.py")
    checks = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checks)
    report = {"success": False, "scope": "standalone actual C++ declaration exporter and HLSL compilation; no GPU",
              "shaders": [], "layout_types": []}
    try:
        driver = output / "export.cpp"
        driver.write_text(EXPORTER)
        exporter = output / "export-shaders"
        run(exporter_command(commands, driver, exporter), output / "exporter-build.log", root)
        run([exporter, output], output / "export.log", root)
        reflected = set()
        for api in ("vulkan", "dx12"):
            target = output / api
            contracts = json.loads((target / "cpu-layout.json").read_text())
            for name, (entry, profile) in SHADERS.items():
                if api != "vulkan" and name.endswith("-subpass"):
                    continue
                flattened = target / (name + ".flattened.hlsl")
                flattened.write_text(checks.flatten_includes((target / (name + ".hlsl")).read_text(),
                    root / "jEngine/Resource/Shaders/hlsl", set()))
                binary = target / (name + (".spv" if api == "vulkan" else ".dxil"))
                command = [dxc, "-T", profile, "-E", entry, "-Fo", binary, flattened]
                if api == "vulkan":
                    command += ["-spirv", "-fspv-target-env=vulkan1.1spirv1.4", "-fvk-use-scalar-layout",
                                "-fspv-use-unknown-image-format"]
                run(command, target / (name + ".log"), root)
                if api == "vulkan":
                    reflected.update(checks.check_layout(binary, contracts))
                report["shaders"].append(f"{api}/{name}")
        if set(contracts) - reflected:
            raise ValueError(f"Unreflected CPU layouts: {sorted(set(contracts) - reflected)}")
        report["volume_ps_fixed_function_depth"] = check_volume_fixed_depth(output / "vulkan/shadow-volume-ps.spv")
        report["point_moment_encoding"] = check_point_moment_encoding(output / "vulkan/shadow-compute-0.spv")
        negative = json.loads((output / "vulkan/cpu-layout.json").read_text())
        negative["jAdvancedShadowData"]["fields"]["FilterParams"] += 4
        try:
            checks.check_layout(output / "vulkan/shadow-reader.spv", negative)
        except ValueError as expected:
            report["negative_layout_control"] = str(expected)
        else:
            raise ValueError("Incorrect CPU FilterParams offset was accepted")
        report.update(success=True, layout_types=sorted(reflected))
        print(f"PASS: {len(report['shaders'])} DXIL/SPIR-V compilations; {len(reflected)} CPU layout contracts")
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
