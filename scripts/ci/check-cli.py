#!/usr/bin/env python3
"""Exercise process-level input validation without a window or GPU."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    args = parser.parse_args()
    engine, source = args.engine.resolve(), args.source.resolve()
    base = os.environ.copy()
    for name in ('JENGINE_MAX_FRAMES', 'JENGINE_FIXED_TIMESTEP', 'JENGINE_SURFEL_GI', 'JENGINE_INLINE_RAY',
                 'JENGINE_TEST_INLINE_TOGGLE', 'JENGINE_TEST_SURFEL_RECONFIGURE', 'JENGINE_TEST_VULKAN_RESOURCES'):
        base.pop(name, None)
    cases = []
    for value in ('-1', '4294967296', '12garbage'):
        cases.append(('JENGINE_MAX_FRAMES', value))
    for value in ('nan', 'inf', '-0.1', '0.01garbage'):
        cases.append(('JENGINE_FIXED_TIMESTEP', value))
    cases += [('JENGINE_SURFEL_GI', '2'), ('JENGINE_INLINE_RAY', 'garbage')]
    for name, value in cases:
        result = subprocess.run([str(engine), '-vulkan'], cwd=source, env={**base, name: value},
                                capture_output=True, text=True, timeout=15)
        output = result.stdout + result.stderr
        if result.returncode != 1 or name not in output or 'jEngine initialized:' in output:
            raise RuntimeError(f'Invalid {name}={value!r} did not fail before initialization: {result.returncode}\n{output}')
        print(f'PASS invalid {name}={value!r} rejected')
    for arguments in (['--ci-export-shaders'], ['--ci-export-shaders', 'unused', 'invalid-api'], ['-vulkan', '-dx12']):
        result = subprocess.run([str(engine), *arguments], cwd=source, env=base, capture_output=True, text=True, timeout=15)
        if result.returncode != 1 or 'jEngine initialized:' in result.stdout:
            raise RuntimeError(f'Invalid arguments accepted: {arguments}\n{result.stdout}\n{result.stderr}')
    result = subprocess.run([str(engine), '--ci-cpu'], cwd=source,
                            env={**base, 'JENGINE_MAX_FRAMES': 'invalid', 'JENGINE_FIXED_TIMESTEP': 'nan'},
                            capture_output=True, text=True, timeout=180)
    if result.returncode or 'CPU checks: PASSED' not in result.stdout:
        raise RuntimeError(f'Headless tests incorrectly depend on runtime options\n{result.stdout}\n{result.stderr}')
    print(f'PASS {len(cases) + 4} process input regressions; no GPU required')


if __name__ == '__main__':
    main()
