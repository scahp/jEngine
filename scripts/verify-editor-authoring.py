#!/usr/bin/env python3
"""Run bounded real editor/GPU workflows and preserve checkout preferences."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def fingerprints(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()} if directory.exists() else {}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=Path('build/macos-debug/jEngine'))
    parser.add_argument('--icd', type=Path)
    parser.add_argument('--api', choices=['vulkan', 'dx12'], default='vulkan')
    parser.add_argument('--output', type=Path, default=Path('build/authoring/verification'))
    parser.add_argument('--case', action='append', choices=['uploads', 'workflow', 'renderer-switch'])
    parser.add_argument('--scene-id', default='Sponza_PathTracing/sponza.glb',
                        help='Scene Browser scene used for renderer-switch verification; prepare its shared materials first.')
    parser.add_argument('--scene-loader', default='Model', choices=['Model', 'PathTracing'])
    parser.add_argument('--wake-display', action='store_true',
                        help='Temporarily wake/hold a real macOS display for native-window validation.')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    engine, output = args.engine.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    ini = root / 'jEngine/jengine.ini'
    original = ini.read_bytes() if ini.exists() else None
    preferences = root / 'jEngine/.jeditor'
    before = fingerprints(preferences)
    report = {'success': False, 'engine_sha256': hashlib.sha256(engine.read_bytes()).hexdigest(),
              'api': args.api, 'cases': {}}
    env = {key: value for key, value in os.environ.items() if not key.startswith('JENGINE_')}
    env['JENGINE_TEST_WAIT_FOR_SCENE'] = '1'
    if args.api == 'vulkan':
        env['VK_INSTANCE_LAYERS'] = 'VK_LAYER_KHRONOS_validation'
        if args.icd:
            env['VK_DRIVER_FILES'] = str(args.icd.resolve())
        if sys.platform == 'darwin':
            env.update(MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING='1', MTL_DEBUG_LAYER='1')
    keep_awake = None
    try:
        if args.wake_display:
            if sys.platform != 'darwin':
                parser.error('--wake-display requires macOS')
            subprocess.run(['/usr/bin/caffeinate', '-u', '-t', '1'], check=True)
            keep_awake = subprocess.Popen(['/usr/bin/caffeinate', '-d', '-t', '600'])
        for case in args.case or ['uploads', 'workflow']:
            destination = output / case
            destination.mkdir(parents=True, exist_ok=True)
            if case == 'uploads':
                command = [str(engine), '--ci-editor-gpu']
            elif case == 'renderer-switch':
                command = [str(engine), '--ci-renderer-switch', str(destination),
                           '--scene-browser', args.scene_id, args.scene_loader]
            else:
                command = [str(engine), '--ci-editor-flow', str(destination)]
            command.append('-' + args.api)
            log_path = destination / 'run.log'
            case_env = {**env, 'JENGINE_LOG_FILE': str(destination / 'engine.jsonl')}
            if case in ('workflow', 'renderer-switch'):
                case_env['JENGINE_CAPTURE_FRAME'] = str(destination / 'workspace-raw.png')
            with log_path.open('w', encoding='utf-8') as log:
                result = subprocess.run(command, cwd=root / 'jEngine', env=case_env,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=360)
            text = log_path.read_text(encoding='utf-8', errors='replace')
            problems = re.findall(r'^.*(?:Validation (?:Error|Warning)|VUID-|\[mvk-error\]|failed assertion|^FAIL ).*$', text, re.M)
            entry = {'exit_code': result.returncode, 'validation_problems': problems,
                     'pass_lines': len(re.findall(r'^PASS ', text, re.M))}
            report['cases'][case] = entry
            if result.returncode or problems:
                raise RuntimeError(f'{case} failed; inspect {log_path}')
            if case == 'uploads':
                if ('UPLOAD_MEASUREMENT scenario=initial' not in text or entry['pass_lines'] < 18
                        or 'editing one real particle color preserves the other emitter' not in text):
                    raise RuntimeError('Upload/particle GPU checks or actual measurements are missing')
                entry['measurements'] = [line for line in text.splitlines() if line.startswith('UPLOAD_')]
            elif case == 'renderer-switch':
                flow = json.loads((destination / 'renderer-switch-gpu-report.json').read_text())
                if (flow['status'] != 'passed' or flow['failureCount']
                        or len(flow['checks']) < 10 or not all(item['pass'] for item in flow['checks'])
                        or not (destination / 'workspace-raw.png').is_file()):
                    raise RuntimeError('Actual viewport renderer switching did not pass')
                entry['renderer_switch_report'] = flow
            else:
                flow = json.loads((destination / 'editor-authoring-gpu-report.json').read_text())
                if (flow['status'] != 'passed' or flow['failureCount'] or flow['authoredEntities'] != 100
                        or flow['maximumPlatformViewports'] < 2 or flow['secondaryRenderedFrames'] < 5
                        or len(flow['checks']) < 20 or not all(item['pass'] for item in flow['checks'])):
                    raise RuntimeError('Actual editor/platform-window assertions did not pass')
                entry['workflow_report'] = flow
            print(f'PASS {case}: {entry["pass_lines"]} actual checks, zero validation problems', flush=True)
        report['success'] = True
    finally:
        if original is not None:
            ini.write_bytes(original)
        elif ini.exists():
            ini.unlink()
        report['checkout_preferences_preserved'] = before == fingerprints(preferences)
        if not report['checkout_preferences_preserved']:
            report['success'] = False
        (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        if keep_awake:
            keep_awake.terminate()
            keep_awake.wait(timeout=5)
    if not report['success']:
        raise RuntimeError('Verification did not complete with preferences preserved')


if __name__ == '__main__':
    main()
