#!/usr/bin/env python3
"""Exercise the production packager and launch its relocated macOS runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


PROBE = r'''
#include "Code/Engine/Editor/jEditorPackaging.h"
#include "FileLoader/jEditorAssets.h"
#include <fstream>
static int VerifyPackageBuild(char** arguments)
{
    jEditorProject project; std::string error;
    if (!project.Open(jEditorAssetPath::FromUtf8(arguments[2]), error)) { std::cerr << error; return 1; }
    jEditorAssetRegistry registry(project);
    if (!registry.Scan(error)) { std::cerr << error; return 1; }
    jEditorPackageRequest request; request.Project = &project; request.Registry = &registry;
    request.OutputDirectory = jEditorAssetPath::FromUtf8(arguments[5]);
    request.StartupScene = project.GetConfig().StartupScene;
    if (!jEditorPackaging::PrepareRuntime(jEditorAssetPath::FromUtf8(arguments[3]),
        jEditorAssetPath::FromUtf8(arguments[4]), request.RuntimeFiles,
        request.RuntimeExecutable, request.FinalizeStaged, error)) { std::cerr << error; return 1; }
    jEditorPackageReport report;
    if (!jEditorPackager::Build(request, report, error)) { std::cerr << error; return 1; }
    if (!jEditorPackager::Validate(report.Manifest, error)) { std::cerr << error; return 1; }
    std::cout << "PASS production package build: files=" << report.FileCount << " assets="
        << report.AssetCount << " bytes=" << report.Bytes << std::endl;
    return 0;
}
'''


def hashes(directory):
    return {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob('*') if path.is_file()}


def build_probe(root, build, output):
    original = (root / 'jEngine/Main.cpp').read_text(encoding='utf-8')
    anchor = 'int main(int argc, char** argv)'
    assert original.count(anchor) == 1
    source = original.replace(anchor, PROBE + '\n' + anchor, 1)
    anchor = '    gCommandLineArgument.Init(argc, argv);'
    assert source.count(anchor) == 1
    source = source.replace(anchor, '''    if (argc == 6 && std::strcmp(argv[1], "--ci-package-probe") == 0)
        return VerifyPackageBuild(argv);
''' + anchor, 1)
    main = output / 'PackageMain.cpp'
    main.write_text(source, encoding='utf-8')
    entry = next(item for item in json.loads((build / 'compile_commands.json').read_text())
                 if item['file'].endswith('/Main.cpp'))
    arguments = shlex.split(entry['command'])
    obj = output / 'PackageMain.cpp.o'
    arguments[arguments.index('-o') + 1] = str(obj)
    arguments[arguments.index('-c') + 1] = str(main)
    with (output / 'probe-build.log').open('w') as log:
        subprocess.run(arguments, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'jEngine'], text=True)
        link = shlex.split(commands.strip().splitlines()[-1])
        if link[:2] == [':', '&&']:
            link = link[2:]
        if link[-2:] == ['&&', ':']:
            link = link[:-2]
        original_object = 'CMakeFiles/jEngine.dir/jEngine/Main.cpp.o'
        assert link.count(original_object) == 1
        link[link.index(original_object)] = str(obj)
        probe = output / 'package-probe'
        link[link.index('-o') + 1] = str(probe)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return probe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build/macos-release'))
    parser.add_argument('--project', type=Path, default=Path('examples/authoring-demo/project.jproject'))
    parser.add_argument('--icd', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='An owned directory with no previous staged package.')
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This executable-relink and launch probe currently targets macOS')
    root = Path(__file__).resolve().parents[1]
    build, project, output = args.build.resolve(), args.project.resolve(), args.output.resolve()
    if output == project.parent or project.parent in output.parents:
        parser.error('Verification output must be outside the source project')
    output.mkdir(parents=True, exist_ok=True)
    before = hashes(project.parent)
    probe = build_probe(root, build, output)
    if args.prepare_only:
        print('PASS prepared production packager probe:', probe)
        return
    engine = build / 'jEngine'
    report = {'success': False, 'source_project_preserved': False,
              'engine_sha256': hashlib.sha256(engine.read_bytes()).hexdigest(),
              'scope': 'Production staged export, actual relocated runtime, same host OS/architecture.'}
    try:
        # Scanning may refresh sidecar fingerprints after a fresh checkout.
        # Keep that expected registry mutation inside the owned source copy.
        copied_project = output / 'source-project'
        shutil.copytree(project.parent, copied_project, symlinks=True,
                        ignore=shutil.ignore_patterns('.git', '.jeditor', 'build', '__pycache__'))
        staged = output / 'staged'
        env = {**os.environ, 'VK_DRIVER_FILES': str(args.icd.resolve()),
               'MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING': '1'}
        with (output / 'package-build.log').open('w') as log:
            subprocess.run([str(probe), '--ci-package-probe', str(copied_project / project.name), str(engine),
                            str(root / 'jEngine/Resource'), str(staged)], cwd=root / 'jEngine',
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
        relocated = output / 'moved package 한글 $literal [x]'
        staged.rename(relocated)
        report['relocated_root'] = str(relocated)
        for key in list(env):
            if key.startswith(('JENGINE_', 'DYLD_')) or key in ('VK_DRIVER_FILES', 'VK_ICD_FILENAMES', 'VK_LAYER_PATH', 'VK_INSTANCE_LAYERS', 'VULKAN_SDK'):
                env.pop(key, None)
        env.update(JENGINE_MAX_FRAMES='120', JENGINE_FIXED_TIMESTEP='0.016666667',
                   JENGINE_CAPTURE_FRAME=str(output / 'runtime-frame-60.png'),
                   JENGINE_LOG_FILE=str(output / 'runtime.jsonl'), MTL_DEBUG_LAYER='1')
        with tempfile.TemporaryDirectory(prefix='jengine-unrelated-cwd-') as unrelated:
            report['launch_working_directory'] = unrelated
            with (output / 'runtime.log').open('w') as log:
                result = subprocess.run([str(relocated / 'run-jEngine.command'), '-vulkan'], cwd=unrelated,
                                        env=env, stdout=log, stderr=subprocess.STDOUT, timeout=300)
        text = (output / 'runtime.log').read_text(errors='replace')
        report['exit_code'] = result.returncode
        report['validation_problems'] = re.findall(r'^.*(?:Validation (?:Error|Warning)|\[mvk-error\]|failed assertion|error:).*$', text, re.M)
        report['capture_exists'] = (output / 'runtime-frame-60.png').is_file()
        report['clean_120_frame_shutdown'] = 'jEngine clean shutdown; rendered frames: 120' in text
        if result.returncode or report['validation_problems'] or not report['capture_exists'] or not report['clean_120_frame_shutdown']:
            raise RuntimeError('Relocated runtime failed; inspect runtime.log')
        report['success'] = True
        print('PASS actual relocated runtime: 120 frames, Unicode/spaces/literal-$ path, unrelated cwd, no SDK/loader overrides')
    finally:
        report['source_project_preserved'] = hashes(project.parent) == before
        report['success'] &= report['source_project_preserved']
        (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if not report['success']:
        raise RuntimeError('Source project changed during package verification')


if __name__ == '__main__':
    main()
