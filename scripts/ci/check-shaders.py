#!/usr/bin/env python3
"""Compile actual engine-generated SurfelGI declarations and check SPIR-V against C++ layout."""
import argparse
import json
import struct
import re
import subprocess
from pathlib import Path

SHADERS = {'candidate-gather': 'main', 'winner-select': 'main', 'visualize': 'main', 'hover-select': 'main', 'snapshot': 'main', 'resolve': 'main', 'placement': 'main', 'gather': 'SurfelGIGatherIrradianceHWRT_CS'}

SHADERS = {name: (entry, 'cs_6_6') for name, entry in SHADERS.items()}
SHADERS.update({
    'editor-gbuffer': ('main', 'ps_6_6'),
    'editor-gbuffer-vs': ('main', 'vs_6_6'),
    'editor-forward': ('main', 'ps_6_6'),
    'shadow-directional': ('main', 'ps_6_6'),
    'shadow-point': ('main', 'ps_6_6'),
    'shadow-spot': ('main', 'ps_6_6'),
    'editor-path-mesh': ('MeshClosestHitShader', 'lib_6_6'),
    'editor-shadow-vs': ('main', 'vs_6_6'),
    'editor-shadow-instancing-vs': ('main', 'vs_6_6'),
    'editor-spot-shadow-vs': ('main', 'vs_6_6'),
    'editor-omni-shadow-vs': ('main', 'vs_6_6'),
    'editor-shadow-ps': ('main', 'ps_6_6'),
    'editor-omni-shadow-ps': ('main', 'ps_6_6'),
    'effect-eye-adaptation': ('main', 'cs_6_6'),
    'effect-bloom-setup': ('main', 'ps_6_6'),
    'effect-bloom-down-vs': ('main', 'vs_6_6'),
    'effect-bloom-down-ps': ('main', 'ps_6_6'),
    'effect-bloom-up-vs': ('main', 'vs_6_6'),
    'effect-bloom-up-ps': ('main', 'ps_6_6'),
    'effect-tonemap': ('main', 'ps_6_6'),
    'effect-gaussian-vertical': ('Vertical', 'cs_6_6'),
    'effect-gaussian-horizontal': ('Horizon', 'cs_6_6'),
    'effect-ssao': ('main', 'cs_6_6'),
    'effect-ssgi': ('main', 'cs_6_6'),
    'effect-ssgi-accumulate': ('main', 'cs_6_6'),
    'effect-ssgi-reprojection': ('SSGIReprojectionPS', 'ps_6_6'),
    'effect-ssgi-reprojection-discontinuity': ('SSGIReprojectionPS', 'ps_6_6'),
    'effect-atmospheric-shadow': ('main', 'cs_6_6'),
    'effect-atmospheric-apply-cs': ('main', 'cs_6_6'),
    'effect-atmospheric-apply-ps': ('main', 'ps_6_6'),
})


def spirv_layout(path):
    data = path.read_bytes()
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError(f'Not SPIR-V: {path}')
    names, members, offsets, arrays, strides = {}, {}, {}, {}, {}
    pos = 5
    def string(values):
        return struct.pack('<' + 'I' * len(values), *values).split(b'\0', 1)[0].decode()
    while pos < len(words):
        length, opcode = words[pos] >> 16, words[pos] & 0xFFFF
        if not length or pos + length > len(words):
            raise ValueError('Invalid SPIR-V instruction')
        args = words[pos + 1:pos + length]
        if opcode == 5: names[args[0]] = string(args[1:])       # OpName
        elif opcode == 6: members[args[0], args[1]] = string(args[2:])
        elif opcode == 29: arrays[args[0]] = args[1]           # OpTypeRuntimeArray
        elif opcode == 71 and args[1] == 6: strides[args[0]] = args[2]
        elif opcode == 72 and args[2] == 35: offsets[args[0], args[1]] = args[3]
        pos += length
    return names, members, offsets, arrays, strides


def check_layout(path, contracts):
    names, members, offsets, arrays, strides = spirv_layout(path)
    checked = set()
    for type_id, name in names.items():
        if name not in contracts: continue
        expected = contracts[name]
        reflected = {member: offsets.get((type_id, index))
                     for (owner, index), member in members.items() if owner == type_id}
        if not reflected: continue
        for member, offset in expected['fields'].items():
            if reflected.get(member) != offset:
                raise ValueError(f'{path.name}: {name}.{member}: C++ offset {offset}, SPIR-V {reflected.get(member)}')
        for array_id, element in arrays.items():
            if element == type_id and strides.get(array_id) != expected['size']:
                raise ValueError(f'{path.name}: {name}: C++ size {expected["size"]}, SPIR-V stride {strides.get(array_id)}')
        checked.add(name)
    return checked


def run(command, log, cwd):
    result = subprocess.run([str(v) for v in command], cwd=cwd, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}): {command}\n{result.stdout}')


def flatten_includes(text, include_dir, seen):
    # Match the engine loader's include-once behavior (its HLSL helpers lack guards).
    def replace(match):
        name = match.group(1)
        path = include_dir / name
        if not path.exists():
            matches = [p for p in include_dir.iterdir() if p.name.casefold() == name.casefold()]
            if len(matches) != 1: raise FileNotFoundError(name)
            path = matches[0]
        key = str(path.resolve()).casefold()
        if key in seen: return ''
        seen.add(key)
        return flatten_includes(path.read_text(encoding='utf-8-sig'), include_dir, seen)
    return re.sub(r'^\s*#include\s+"([^"\n]+)"', replace, text, flags=re.MULTILINE)


def main():
    parser = argparse.ArgumentParser()
    for option in ('engine', 'dxc', 'source', 'output'): parser.add_argument('--' + option, required=True, type=Path)
    args = parser.parse_args()
    args.engine, args.dxc, args.source, args.output = [v.resolve() for v in (args.engine, args.dxc, args.source, args.output)]
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'success': False, 'shaders': [], 'layout_types': []}
    try:
        checked = set()
        for api in ('vulkan', 'dx12'):
            target = args.output / api
            target.mkdir(exist_ok=True)
            run([args.engine, '--ci-export-shaders', target, api], target / 'export.log', args.source)
            contracts = json.loads((target / 'cpu-layout.json').read_text())
            contracts.update(json.loads((target / 'editor-cpu-layout.json').read_text()))
            for name, (entry, profile) in SHADERS.items():
                output = target / (name + ('.spv' if api == 'vulkan' else '.dxil'))
                flattened = target / (name + '.flattened.hlsl')
                flattened.write_text(flatten_includes((target / (name + '.hlsl')).read_text(), args.source / 'Resource/Shaders/hlsl', set()))
                command = [args.dxc, '-T', profile, '-E', entry, '-I', args.source / 'Resource/Shaders/hlsl',
                           '-Fo', output, flattened]
                if api == 'vulkan':
                    command += ['-spirv', '-fspv-target-env=vulkan1.1spirv1.4', '-fvk-use-scalar-layout',
                                '-fspv-use-unknown-image-format', '-fspv-extension=SPV_EXT_descriptor_indexing',
                                '-fspv-extension=SPV_KHR_ray_tracing', '-fspv-extension=SPV_KHR_ray_query',
                                '-fspv-extension=SPV_EXT_shader_viewport_index_layer']
                run(command, target / (name + '.log'), args.source)
                if api == 'vulkan': checked.update(check_layout(output, contracts))
                report['shaders'].append(f'{api}/{name}')
        # Recreate the original regression: valid scalar-layout SPIR-V without
        # the CPU alignment offsets. It must compile, then fail the ABI check.
        control = args.output / 'unaligned-control.hlsl'
        original = (args.output / 'vulkan/resolve.flattened.hlsl').read_text()
        control.write_text(re.sub(r'\[\[vk::offset\(\d+\)\]\]', '', original))
        control_spv = args.output / 'unaligned-control.spv'
        run([args.dxc, '-T', 'cs_6_6', '-E', 'main', '-spirv', '-fvk-use-scalar-layout',
             '-fspv-use-unknown-image-format', '-Fo', control_spv, control],
            args.output / 'unaligned-control.log', args.source)
        try:
            check_layout(control_spv, contracts)
        except ValueError as expected:
            report['negative_layout_control'] = str(expected)
        else:
            raise ValueError('ABI regression control was incorrectly accepted')
        missing = set(contracts) - checked
        if missing: raise ValueError(f'CPU contracts were not reflected: {sorted(missing)}')
        report.update(success=True, layout_types=sorted(checked))
        print(f'PASS: {len(report["shaders"])} shader compilations; {len(checked)} CPU/SPIR-V type contracts; no GPU required')
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
