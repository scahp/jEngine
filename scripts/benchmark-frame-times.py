#!/usr/bin/env python3
"""Compare bounded real rendering loops; this measures average loop time, not GPU timestamps."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import statistics
import subprocess
import threading
import time


def run(engine, icd, directory, label, trial, root, require_publication):
    env = {k: v for k, v in os.environ.items() if not k.startswith('JENGINE_')}
    env.update(VK_DRIVER_FILES=str(icd), MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING='1',
               MVK_CONFIG_LOG_LEVEL='1', JENGINE_MAX_FRAMES='360',
               JENGINE_FIXED_TIMESTEP='0.016666667', JENGINE_SURFEL_GI='1', JENGINE_INLINE_RAY='1',
               JENGINE_LOG_FILE=str(directory / f'{label}-{trial}.jsonl'), JENGINE_BENCHMARK_SCENE_MARKERS='1')
    env.pop('VK_INSTANCE_LAYERS', None)
    env.pop('MTL_DEBUG_LAYER', None)
    started = time.perf_counter()
    process = subprocess.Popen([str(engine), '-vulkan'], cwd=root / 'jEngine', env=env,
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
        if code or not clean or initialized is None or not {120, 360} <= frames.keys():
            raise RuntimeError(f'{label} failed: exit={code}, frames={list(frames)}')
        if require_publication and (published is None or published > frames[120]):
            raise RuntimeError('Saved scene was not published before the 120-frame measurement boundary')
        seconds = frames[360] - frames[120]
        return {'label': label, 'trial': trial, 'time_to_initialized_ms': (initialized - started) * 1000,
                'scene_published_before_measurement': not require_publication or published <= frames[120],
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
              'scene': 'Same saved model scene; baseline restores synchronously, new binary must emit a publication marker before frame120.',
              'engine_sha256': {k: hashlib.sha256(v.read_bytes()).hexdigest() for k, v in engines.items()},
              'samples': []}
    settings = root / 'jEngine/jengine.ini'
    original = settings.read_bytes() if settings.exists() else None
    saved_scene = re.search(rb'^LastLoadedSceneId=\"?([^\"\r\n]+)', original or b'', flags=re.M)
    try:
        for trial in range(args.runs):
            order = ['before', 'after'] if trial % 2 == 0 else ['after', 'before']
            for label in order:
                if original is not None:
                    settings.write_bytes(original)
                elif settings.exists():
                    settings.unlink()
                sample = run(engines[label], args.icd.resolve(), output, label, trial, root, label == 'after' and bool(saved_scene))
                report['samples'].append(sample)
                print(json.dumps(sample), flush=True)
        report['summary'] = {label: {'median_loop_ms': statistics.median(
            x['loop_mean_ms'] for x in report['samples'] if x['label'] == label),
            'median_time_to_initialized_ms': statistics.median(x['time_to_initialized_ms'] for x in report['samples'] if x['label'] == label)}
            for label in engines}
        report['success'] = True
    finally:
        if original is not None:
            settings.write_bytes(original)
        elif settings.exists():
            settings.unlink()
        (output / 'frame-times.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
