#!/usr/bin/env python3
"""Bounded M4 GPU integration checks for submission, AS updates and effects."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=Path('build/macos-debug/jEngine'))
    parser.add_argument('--icd', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/full-audit/gpu'))
    parser.add_argument('--case', action='append', choices=['resources', 'odd', 'tiny'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    engine, icd, output = args.engine.resolve(), args.icd.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for name in ('JENGINE_TEST_EFFECTS', 'JENGINE_TEST_VULKAN_RESOURCES', 'JENGINE_TEST_SURFEL_RECONFIGURE', 'JENGINE_TEST_INLINE_TOGGLE'):
        env.pop(name, None)
    report = {'success': False, 'engine_sha256': hashlib.sha256(engine.read_bytes()).hexdigest(), 'cases': {}}
    try:
        selected = set(args.case or ['resources', 'odd', 'tiny'])
        if 'resources' in selected:
            resource_dir = output / 'resources'
            subprocess.run([sys.executable, str(root / 'scripts/verify-surfel-gi.py'), '--engine', str(engine), '--icd', str(icd),
                            '--mode', 'on', '--output', str(resource_dir)], cwd=root,
                           env={**env, 'JENGINE_TEST_VULKAN_RESOURCES': '1', 'JENGINE_TEST_WAIT_FOR_SCENE': '1', 'JENGINE_BENCHMARK_SCENE_MARKERS': '1'}, check=True, timeout=360)
            resource_log = (resource_dir / 'on.log').read_text()
            assert 'Scene import published:' in resource_log, 'new async Model import did not publish'
            assert 'readback_words=256 mismatches=0 submitted_fence_signaled=1' in resource_log
            updates = re.findall(r'tlas_update_frame=(\d+) advanced=(\d+)', resource_log)
            assert {31, 61, 91} <= {int(frame) for frame, advanced in updates if advanced == '1'}, updates
            report['cases']['resources'] = {'readback_words': 256, 'mismatches': 0, 'tlas_updates': updates}
            print('PASS real GPU readback, split submissions and moving TLAS updates', flush=True)
        for mode, dimensions in [('odd', (1283, 723)), ('tiny', (17, 17))]:
            if mode not in selected: continue
            capture = output / f'effects-{mode}.png'
            log_path = output / f'effects-{mode}.log'
            effects_env = {**env, 'VK_DRIVER_FILES': str(icd), 'MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING': '1',
                           'MVK_CONFIG_LOG_LEVEL': '2', 'VK_INSTANCE_LAYERS': 'VK_LAYER_KHRONOS_validation', 'MTL_DEBUG_LAYER': '1',
                           'JENGINE_TEST_EFFECTS': mode, 'JENGINE_MAX_FRAMES': '120', 'JENGINE_TEST_WAIT_FOR_SCENE': '1', 'JENGINE_FIXED_TIMESTEP': '0.016666667',
                           'JENGINE_CAPTURE_FRAME': str(capture)}
            with log_path.open('w') as log:
                subprocess.run([str(engine), '-vulkan'], cwd=root / 'jEngine', env=effects_env,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
            text = log_path.read_text()
            assert 'jEngine clean shutdown; rendered frames: 120' in text, mode
            assert not re.search(r'Validation (Error|Warning):|\[mvk-error\]|failed assertion|error:', text), mode
            exposure = re.findall(r'Effects audit: frame=(\d+) width=(\d+) height=(\d+) exposure_finite_positive=(\d+) exposure_half=(\d+)', text)
            assert {1, 2, 90} <= {int(row[0]) for row in exposure}, (mode, exposure)
            assert all(row[3] == '1' and (int(row[1]), int(row[2])) == dimensions for row in exposure), (mode, exposure)
            png = capture.read_bytes()
            assert png[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II', png, 16) == dimensions
            report['cases'][mode] = {'dimensions': dimensions, 'exposure': exposure, 'frames': 120, 'validation_errors': 0}
            print(f'PASS {mode} effects: bloom, exposure, SSAO, SSGI history/reprojection/blur and atmosphere', flush=True)
        report['success'] = True
    finally:
        (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
