#!/usr/bin/env python3
"""Compile deferred sample-frequency/MRT/resolve permutations from production C++ parameter sets."""
import argparse
import importlib.util
import json
from pathlib import Path

EXPORTER = r'''
#define XXH_INLINE_ALL
#include "pch.h"
#include "Shader/jLightingShaderParameters.h"
#include "Shader/jDeferredResolveShaderParameters.h"
#include "Shader/jMultisampleInspectParameters.h"
#include "Shader/jSkinShadingShaderParameters.h"
#include <iomanip>
static_assert(sizeof(RenderObjectUniformBuffer)==192);
static_assert(sizeof(jMaterialGraphUniformBuffer)==1232);
static_assert(offsetof(jMaterialGraphUniformBuffer,MaterialSkinParameters)==1216);
static bool Vulkan = true;
bool IsUseVulkan() { return Vulkan; }
bool IsUseDX12() { return !Vulkan; }
template<class T>void Layout(std::ostream& file,bool& first)
{
    if(!first)file<<",";first=false;
    file<<std::quoted(T::GetShaderUniformBufferTypeName())<<":{\"size\":"<<sizeof(T)<<",\"fields\":{";
    bool firstField=true;
    for(const auto& field:T::GetShaderUniformBufferFieldMembers())
    {if(!firstField)file<<",";firstField=false;file<<std::quoted(field.Name)<<":"<<field.ByteOffset;}
    file<<"}}";
}
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    for (bool api : {true, false})
    {
        Vulkan = api;
        auto target = std::filesystem::path(argv[1]) / (api ? "vulkan" : "dx12");
        std::filesystem::create_directories(target);
        auto write = [&](std::string name, std::string flags, std::string declarations, std::string file)
        {
            std::ofstream out(target / (name + ".hlsl"));
            out << "#include \"common.hlsl\"\n" << flags << declarations << "\n#include \"" << file << "\"\n";
        };
        for (int skin : {0, 1}) for (int samples : {0, 1}) for (int sss : {0, 1}) for (int pbr : {0, 1})
        {
            std::string flags = "#define PIXEL_SHADER 1\n#define USE_SUBPASS 0\n#define USE_SHADOW_MAP 1\n#define USE_REVERSEZ 0\n#define USE_VARIABLE_SHADING_RATE 0\n";
            flags += "#define USE_MSAA " + std::to_string(samples) + "\n#define USE_SUBSURFACE " + std::to_string(sss)
                + "\n#define USE_PBR " + std::to_string(pbr) + "\n#define USE_SKIN_MATERIAL " + std::to_string(skin) + "\n";
            auto scene = samples ? jShaderParameterSet::GenerateHLSL<jSceneMultisampleTexturesShaderParameters>(3)
                : jShaderParameterSet::GenerateHLSL<jSceneTexturesShaderParameters>(3);
            auto view = jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0);
            if(skin)scene+=jShaderParameterSet::GenerateHLSL<jSkinEnergyShaderParameters>(4);
            const auto suffix = (skin?std::string("-skin"):std::string()) + "-" + std::to_string(samples) + std::to_string(sss) + std::to_string(pbr);
            write("directional" + suffix, flags, view + jShaderParameterSet::GenerateHLSL<jDirectionalLightShaderParameters>(1)
                + jShaderParameterSet::GenerateHLSL<jDirectionalLightIBLShaderParameters>(2) + scene, "directionallight_fs.hlsl");
            write("point" + suffix, flags, view + jShaderParameterSet::GenerateHLSL<jPointLightShaderParameters>(1)
                + jShaderParameterSet::GenerateHLSL<jLightVolumeVertexShaderParameters>(2) + scene, "pointlight_fs.hlsl");
            write("spot" + suffix, flags, view + jShaderParameterSet::GenerateHLSL<jSpotLightShaderParameters>(1)
                + jShaderParameterSet::GenerateHLSL<jLightVolumeVertexShaderParameters>(2) + scene, "spotlight_fs.hlsl");
        }
        for(int sss:{0,1})for(int uv:{0,1})
        {
            const auto flags="#define PIXEL_SHADER 1\n#define USE_PBR 1\n#define USE_ALBEDO_TEXTURE 0\n#define USE_VERTEX_COLOR 0\n#define USE_VARIABLE_SHADING_RATE 0\n#define USE_SKIN_MATERIAL 1\n#define USE_SUBSURFACE "
                +std::to_string(sss)+"\n#define USE_SKIN_UV "+std::to_string(uv)+"\n";
            const auto material=jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0)
                +jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(1)
                +jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(2);
            write("gbuffer-skin-"+std::to_string(sss)+"-"+std::to_string(uv),flags,material,"gbuffer_fs.hlsl");
        }
        for (int sss : {0, 1})
        {
            const auto flags = "#define USE_SUBSURFACE " + std::to_string(sss) + "\n";
            auto declarations = sss ? jShaderParameterSet::GenerateHLSL<jDeferredResolveSubsurfaceParameters>(0)
                : jShaderParameterSet::GenerateHLSL<jDeferredResolveParameters>(0);
            write("resolve-" + std::to_string(sss), flags, declarations, "deferred_resolve_ps.hlsl");
            auto material = jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0)
                + jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(1)
                + jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(2);
            write("gbuffer-" + std::to_string(sss), flags + "#define PIXEL_SHADER 1\n#define USE_PBR 1\n#define USE_ALBEDO_TEXTURE 0\n#define USE_VERTEX_COLOR 0\n#define USE_VARIABLE_SHADING_RATE 0\n", material, "gbuffer_fs.hlsl");
        }
        write("resolve-vs", "", "", "deferred_resolve_vs.hlsl");
        write("subsurface-copy", "", jShaderParameterSet::GenerateHLSL<jSubsurfaceCopyParameters>(0), "subsurface_copy_ps.hlsl");
        write("inspect-cs", "", jShaderParameterSet::GenerateHLSL<jMultisampleInspectParameters>(0), "msaa_inspect_cs.hlsl");
        write("skin-normal-decode", "", jShaderParameterSet::GenerateHLSL<jSkinNormalDecodeParameters>(0), "skin_normal_decode_ps.hlsl");
        write("skin-uv-resolve", "", jShaderParameterSet::GenerateHLSL<jSkinUVResolveParameters>(0), "skin_uv_resolve_ps.hlsl");
        std::ofstream layout(target/"cpu-layout.json");bool first=true;layout<<"{";
        Layout<RenderObjectUniformBuffer>(layout,first);Layout<ViewUniformBuffer>(layout,first);
        Layout<jMaterialGraphUniformBuffer>(layout,first);layout<<"}\n";
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
    parser.add_argument('--output', type=Path, default=Path('build/deferred-msaa/shader-checks'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    driver = module(root / 'scripts/ci/check-advanced-shadow-shaders.py', 'msaa_driver')
    checks = module(root / 'scripts/ci/check-shaders.py', 'msaa_checks')
    report = {'success': False, 'shaders': [], 'scope': 'production shader compilation; no GPU'}
    try:
        source, exporter = output / 'export.cpp', output / 'export-shaders'
        source.write_text(EXPORTER, encoding='utf-8')
        driver.run(driver.exporter_command(root / 'build/macos-debug/compile_commands.json', source, exporter), output / 'export-build.log', root)
        driver.run([exporter, output], output / 'export.log', root)
        reflected = set()
        for api in ('vulkan', 'dx12'):
            contracts = json.loads((output / api / 'cpu-layout.json').read_text())
            for shader in sorted((output / api).glob('*.hlsl')):
                if shader.name.endswith('.flattened.hlsl'): continue
                text = checks.flatten_includes(shader.read_text(), root / 'jEngine/Resource/Shaders/hlsl', set())
                flat = shader.with_suffix('.flattened.hlsl')
                flat.write_text(text)
                binary = shader.with_suffix('.spv' if api == 'vulkan' else '.dxil')
                command = [root / '.ci-tools/dxc/bin/dxc', '-T', 'vs_6_0' if shader.stem.endswith('-vs') else 'cs_6_0' if shader.stem.endswith('-cs') else 'ps_6_0', '-E', 'main', '-Fo', binary, flat]
                if api == 'vulkan':
                    command += ['-spirv', '-fspv-target-env=vulkan1.1spirv1.4', '-fvk-use-scalar-layout', '-fspv-use-unknown-image-format']
                driver.run(command, shader.with_suffix('.log'), root)
                if api == 'vulkan':
                    reflected.update(checks.check_layout(binary, contracts))
                report['shaders'].append(f'{api}/{shader.stem}')
        if set(contracts) != reflected:
            raise ValueError(f'Production material layouts were not reflected: {sorted(set(contracts) - reflected)}')
        negative = json.loads((output / 'vulkan/cpu-layout.json').read_text())
        negative['jMaterialGraphUniformBuffer']['fields']['MaterialSkinParameters'] = 1200
        try:
            checks.check_layout(output / 'vulkan/gbuffer-skin-1-1.spv', negative)
        except ValueError as expected:
            report['negative_skin_layout_control'] = str(expected)
        else:
            raise ValueError('An incorrect 1200-byte skin material field offset was accepted')
        report['layout_types'] = sorted(reflected)
        report['material_bytes'] = 1232
        report['success'] = True
        print(f"PASS {len(report['shaders'])} production deferred/skin/MSAA shaders; 1232-byte material ABI and negative skin offset control")
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
