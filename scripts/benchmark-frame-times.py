#!/usr/bin/env python3
"""Compare bounded real rendering loops; this measures average loop time, not GPU timestamps."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import statistics
import subprocess
import threading
import time


def run(engine, icd, directory, label, trial, root, require_publication, wait_for_scene, working_directory=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('JENGINE_')}
    env.update(VK_DRIVER_FILES=str(icd), MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING='1',
               MVK_CONFIG_LOG_LEVEL='1', JENGINE_MAX_FRAMES='360',
               JENGINE_FIXED_TIMESTEP='0.016666667', JENGINE_SURFEL_GI='1', JENGINE_INLINE_RAY='1',
               JENGINE_LOG_FILE=str(directory / f'{label}-{trial}.jsonl'), JENGINE_BENCHMARK_SCENE_MARKERS='1',
               JENGINE_TEST_WAIT_FOR_SCENE='1' if wait_for_scene else '0')
    env.pop('VK_INSTANCE_LAYERS', None)
    env.pop('MTL_DEBUG_LAYER', None)
    started = time.perf_counter()
    process = subprocess.Popen([str(engine), '-vulkan'], cwd=working_directory or root / 'jEngine', env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               encoding='utf-8', errors='replace', bufsize=1)
    lines = queue.Queue()

    def read_output():
        for line in process.stdout:
            lines.put((time.perf_counter(), line))
        lines.put((time.perf_counter(), None))

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    initialized = None
    frames = {}
    published = None
    clean = False
    try:
        with (directory / f'{label}-{trial}.log').open('w') as output:
            while True:
                if time.perf_counter() - started > 300:
                    raise TimeoutError(f'{label} rendering timed out')
                try:
                    stamp, line = lines.get(timeout=1)
                except queue.Empty:
                    continue
                if line is None:
                    break
                output.write(line)
                if 'jEngine initialized:' in line:
                    initialized = stamp
                if line.startswith('Scene import published:'):
                    published = stamp
                match = re.search(r'^Rendered frames: (\d+)', line)
                if match:
                    frames[int(match.group(1))] = stamp
                clean |= 'jEngine clean shutdown; rendered frames: 360' in line
        code = process.wait(timeout=10)
        if code or not clean or initialized is None or not {1, 120, 360} <= frames.keys():
            raise RuntimeError(f'{label} failed: exit={code}, frames={list(frames)}')
        if require_publication and (published is None or published > frames[120]):
            raise RuntimeError('Saved scene was not published before the 120-frame measurement boundary')
        if wait_for_scene and require_publication and published > frames[1]:
            raise RuntimeError('Controlled benchmark requires publication before the first rendered frame')
        seconds = frames[360] - frames[120]
        return {'label': label, 'trial': trial, 'time_to_initialized_ms': (initialized - started) * 1000,
                'scene_published_before_measurement': not require_publication or published <= frames[120],
                'scene_published_before_first_frame': not require_publication or published <= frames[1],
                'warmup_frames': 120, 'measured_frames': 240,
                'loop_mean_ms': seconds * 1000 / 240, 'loop_fps': 240 / seconds}
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        reader.join(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-engine', type=Path, required=True)
    parser.add_argument('--after-engine', type=Path, required=True)
    parser.add_argument('--icd', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--background-import', action='store_true',
                        help='Trace normal async restore instead of equalizing scene history before frame one')
    parser.add_argument('--static-sponza', action='store_true',
                        help='Use isolated runtime folders with Sponza light callbacks disabled for an equal static scene.')
    parser.add_argument('--before-source', type=Path,
                        help='Checkout matching the before executable, for its shader and legacy UI runtime inputs.')
    args = parser.parse_args()
    if not 1 <= args.runs <= 10:
        parser.error('--runs must be between 1 and 10')
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    engines = {'before': args.before_engine.resolve(), 'after': args.after_engine.resolve()}
    report = {'success': False, 'conditions': 'Release binaries, same working directory/ini/driver, '
              'fixed simulation timestep, sequential alternating processes; no forced validation; '
              '120-frame warmup, 240-frame wall-clock loop average. Includes CPU/GPU/present waits; '
              'not per-frame p95, isolated GPU time or an unconstrained FPS measurement.',
              'scene': 'Same saved Model scene; baseline restores before draw one, new CPU-worker/owner publication is awaited before draw one by default.',
              'scene_load_mode': 'background' if args.background_import else 'ready-before-first-frame',
              'engine_sha256': {k: hashlib.sha256(v.read_bytes()).hexdigest() for k, v in engines.items()},
              'samples': []}
    settings = root / 'jEngine/jengine.ini'
    original = settings.read_bytes() if settings.exists() else None
    saved_scene = re.search(rb'^LastLoadedSceneId=\"?([^\"\r\n]+)', original or b'', flags=re.M)
    preset = root / 'jEngine/Resource/sponza_pbr/objects.json'
    preset_original = preset.read_bytes() if args.static_sponza else None
    if args.static_sponza:
        if not saved_scene or saved_scene.group(1) != b'sponza_pbr/sponza.glb':
            parser.error('--static-sponza requires the saved sponza_pbr/sponza.glb Model scene')
        controlled = json.loads(preset_original)
        removed_callbacks = sum('logic' in light for light in controlled.get('lights', []))
        for light in controlled.get('lights', []):
            light.pop('logic', None)
        report['scene'] = 'Same Sponza model/camera/static lights; legacy light callbacks disabled in isolated preset copies for both binaries.'
        report['conditions'] = report['conditions'].replace('same working directory/ini/driver',
            'isolated working directories with identical INI/resource layout/driver')
        report['ui'] = 'Baseline classic UI versus current default authoring UI, including scene-preview copy and extra material G-buffer.'
        report['static_preset'] = {'original_sha256': hashlib.sha256(preset_original).hexdigest(),
                                   'controlled': controlled, 'removed_light_callbacks': removed_callbacks}
        before_source = args.before_source.resolve() if args.before_source else root
        before_resource = before_source / 'jEngine/Resource'
        if json.loads((before_resource / 'sponza_pbr/objects.json').read_text()) != json.loads(preset_original):
            parser.error('The source Sponza camera/light presets must match before making controlled copies')
        asset_names = ['sponza_pbr/sponza.glb', 'stpeters_probe_cubemp.dds',
                       'stpeters_probe_irradiancemap.dds', 'stpeters_probe_filteredenvmap.dds']
        asset_hashes = {}
        for name in asset_names:
            old_hash = hashlib.sha256((before_resource / name).read_bytes()).hexdigest()
            new_hash = hashlib.sha256((root / 'jEngine/Resource' / name).read_bytes()).hexdigest()
            if old_hash != new_hash:
                parser.error('Rendering assets differ between source checkouts: ' + name)
            asset_hashes[name] = old_hash
        report['identical_asset_sha256'] = asset_hashes
        report['runtime_inputs'] = {}
        for label, source_root in [('before', before_source), ('after', root)]:
            shader_root = source_root / 'jEngine/Resource/Shaders'
            digest = hashlib.sha256()
            for file in sorted(path for path in shader_root.rglob('*') if path.is_file()):
                digest.update(str(file.relative_to(shader_root)).encode())
                digest.update(b'\0'); digest.update(file.read_bytes())
            report['runtime_inputs'][label] = {'checkout': str(source_root),
                'matching_shader_tree_sha256': digest.hexdigest()}
        report['conditions'] += ' Each executable uses the shader files and legacy UI binaries from its own source revision.'
    try:
        for trial in range(args.runs):
            order = ['before', 'after'] if trial % 2 == 0 else ['after', 'before']
            for label in order:
                working_directory = None
                if args.static_sponza:
                    working_directory = output / f'runtime-{label}-{trial}'
                    working_directory.mkdir(exist_ok=False)
                    resource = working_directory / 'Resource'
                    resource.mkdir()
                    input_root = before_source if label == 'before' else root
                    (working_directory / 'External').symlink_to(input_root / 'jEngine/External', target_is_directory=True)
                    for entry in (input_root / 'jEngine/Resource').iterdir():
                        if entry.name != 'sponza_pbr':
                            (resource / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
                            continue
                        target = resource / entry.name
                        target.mkdir()
                        for child in entry.iterdir():
                            if child.name == 'objects.json':
                                (target / child.name).write_text(json.dumps(controlled, indent=2) + '\n')
                            elif child.is_dir():
                                shutil.copytree(child, target / child.name)
                            else:
                                # Scene IDs use canonical paths relative to the
                                # resource root. A model symlink escapes that
                                # root and cannot match the saved scene ID.
                                shutil.copy2(child, target / child.name)
                    (working_directory / 'jengine.ini').write_bytes(original)
                elif original is not None:
                    settings.write_bytes(original)
                elif settings.exists():
                    settings.unlink()
                sample = run(engines[label], args.icd.resolve(), output, label, trial, root,
                             (args.static_sponza or label == 'after') and bool(saved_scene),
                             not args.background_import, working_directory)
                report['samples'].append(sample)
                print(json.dumps(sample), flush=True)
        report['summary'] = {label: {'median_loop_ms': statistics.median(
            x['loop_mean_ms'] for x in report['samples'] if x['label'] == label),
            'median_time_to_initialized_ms': statistics.median(x['time_to_initialized_ms'] for x in report['samples'] if x['label'] == label)}
            for label in engines}
        report['success'] = True
    finally:
        if args.static_sponza:
            report['source_preset_preserved'] = preset.read_bytes() == preset_original
            report['success'] &= report['source_preset_preserved']
        if args.static_sponza:
            report['source_ini_preserved'] = settings.read_bytes() == original
            report['success'] &= report['source_ini_preserved']
        elif original is not None:
            settings.write_bytes(original)
        elif settings.exists():
            settings.unlink()
        (output / 'frame-times.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
