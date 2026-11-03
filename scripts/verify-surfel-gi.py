#!/usr/bin/env python3
"""Bounded macOS GPU regressions; does not activate or install a runtime."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=Path('build/macos-debug/jEngine'))
    parser.add_argument('--icd', type=Path, required=True, help='Verified experimental MoltenVK ICD')
    parser.add_argument('--output', type=Path, default=Path('build/surfel-audit'))
    parser.add_argument('--mode', action='append', choices=['on', 'off', 'toggle', 'stable', 'reconfigure'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    engine, icd, output = args.engine.resolve(), args.icd.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = {}
    for mode in args.mode or ['on', 'off', 'toggle', 'stable', 'reconfigure']:
        driver = icd
        if mode == 'stable':
            prefix = subprocess.check_output(['brew', '--prefix', 'molten-vk'], text=True).strip()
            driver = Path(prefix) / 'etc/vulkan/icd.d/MoltenVK_icd.json'
        env = os.environ.copy()
        for key in ('JENGINE_TEST_INLINE_TOGGLE', 'JENGINE_TEST_SURFEL_RECONFIGURE'):
            env.pop(key, None)
        env.update(VK_DRIVER_FILES=str(driver), MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING='1',
                   MVK_CONFIG_LOG_LEVEL='2', VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation', MTL_DEBUG_LAYER='1',
                   JENGINE_SURFEL_GI='1', JENGINE_INLINE_RAY='0' if mode == 'off' else '1',
                   JENGINE_SURFEL_DIAGNOSTICS='1', JENGINE_FIXED_TIMESTEP='0.016666667', JENGINE_MAX_FRAMES='120',
                   JENGINE_CAPTURE_FRAME=str(output / f'{mode}.png'))
        if mode == 'toggle': env['JENGINE_TEST_INLINE_TOGGLE'] = '1'
        if mode == 'reconfigure': env['JENGINE_TEST_SURFEL_RECONFIGURE'] = '1'
        with (output / f'{mode}.log').open('w') as log:
            subprocess.run([str(engine), '-vulkan'], cwd=root / 'jEngine', env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=300)
        text = (output / f'{mode}.log').read_text()
        assert 'jEngine clean shutdown; rendered frames: 120' in text, mode
        assert not re.search(r'Validation (Error|Warning):|\[mvk-error\]|failed assertion|error:', text), mode
        invariants = re.findall(r'Surfel regression: snapshot_invalid=(\d+) winner_mismatches=(\d+) candidates=(\d+)', text)
        assert invariants and all(int(a) == 0 and int(b) == 0 for a, b, _ in invariants), (mode, invariants)
        invalid_pixels = re.findall(r'Surfel resolve invalid pixels: (\d+)', text)
        assert invalid_pixels and all(int(v) == 0 for v in invalid_pixels), (mode, invalid_pixels)
        diagnostics = re.findall(r'SurfelGI diagnostics: placed=(\d+) sampled=(\d+) lit=(\d+) invalid=(\d+) energy=([\d.e+-]+) history=([\d.e+-]+)', text)
        assert diagnostics and all(int(v[0]) > 0 and int(v[3]) == 0 for v in diagnostics), (mode, diagnostics)
        nonzero = re.findall(r'Surfel resolve nonzero pixels: (\d+)', text)
        if mode in ('on', 'toggle', 'reconfigure'):
            assert int(diagnostics[-1][2]) > 0 and nonzero and int(nonzero[-1]) > 0, mode
        else:
            assert int(diagnostics[-1][1]) == 0 and float(diagnostics[-1][4]) == 0, mode
        dispatches = [tuple(map(float, v)) for v in re.findall(r'Inline gather dispatches: (\d+) anchor_history=([\d.e+-]+)', text)]
        if mode == 'toggle':
            assert dispatches[0] == dispatches[1] and dispatches[2][0] > dispatches[1][0], dispatches
        if mode == 'reconfigure':
            resets = re.findall(r'Surfel clipmap reset: frame=(\d+) cascade=(\d+) page_size=(\d+)', text)
            assert len({int(frame) for frame, _, _ in resets}) >= 3, resets
            assert {'2', '5'} <= {page for _, _, page in resets}, resets
            capacities = re.findall(r'max_surfels=(\d+)', text)
            assert capacities and len(set(capacities)) == 1, capacities
            assert 'SurfelGI skipped: clipmap or candidate count exceeds the supported GPU budget' in text
        alpha = subprocess.check_output(['sips', '-g', 'hasAlpha', str(output / f'{mode}.png')], text=True)
        assert 'hasAlpha: no' in alpha, mode
        results[mode] = dict(invariants=invariants, diagnostics=diagnostics, dispatches=dispatches, invalid_pixels=invalid_pixels)
        print(f'PASS {mode}: history snapshot, candidate winners, finite resolve, validation and shutdown', flush=True)
    if 'on' in results and 'off' in results:
        comparison = subprocess.check_output([sys.executable, str(root / 'platform/macos/tests/compare-captures.py'),
                                              str(output / 'on.png'), str(output / 'off.png')], text=True)
        results['image_comparison'] = json.loads(comparison)
    (output / 'regressions.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
