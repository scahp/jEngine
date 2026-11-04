#!/usr/bin/env python3
"""Build a disposable real-editor capture driver; --run explicitly admits GPU work.

The default action only compiles/links against the existing Debug objects. It
does not rebuild the shared engine, edit production Main, or launch the GPU.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
OWNED = ROOT / "build/authoring/manual"
OWNED_OUTPUTS = (OWNED, ROOT / "build/authoring/manual-final", ROOT / "build/authoring/manual-published",
                 ROOT / "build/authoring/manual-release")
MODES = ("overview", "tools", "settings")
PROBLEMS = re.compile(
    r"^.*(?:Validation (?:Error|Warning)|VUID-|\[mvk-error\]|failed assertion|"
    r"Assertion failed|Metal Validation|validate.*failed|MTLDebug.*(?:error|assert)|"
    r"(?:^| )error:|^FAIL ).*$", re.M | re.I)

DRIVER = r'''
#include "Code/Engine/Editor/jEditorTools.h"
#include "Code/Engine/Editor/jEditorProjectSettings.h"
#include "Scene/jCamera.h"

static std::string ManualMode()
{
    const char* value = std::getenv("JENGINE_MANUAL_MODE");
    return value ? value : "overview";
}
static void PrepareManualCaptureState()
{
    const char* state = std::getenv("JENGINE_MANUAL_STATE");
    if (!state || !*state) throw std::runtime_error("Manual capture needs an owned state directory");
    SetAuthoringTestStateDirectory(std::filesystem::path(state));
    SCR_WIDTH = 1280; SCR_HEIGHT = 900;
}
static void DrawManualTools()
{
    if (!g_Editor) return;
    ImGui::SetNextWindowPos(ImVec2(285, 55), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(700, 790), ImGuiCond_Once);
    if (ImGui::Begin("Authoring tools"))
        jEditorTools::Render(*g_Editor);
    ImGui::End();
}
static void PrepareManualCaptureView()
{
    if (!g_Editor || !g_Engine || !g_Engine->Game.MainCamera)
        throw std::runtime_error("Actual initialized editor/camera unavailable");
    ImGui::GetIO().FontGlobalScale = 1.15f;
    auto* camera = g_Engine->Game.MainCamera;
    const Vector position(0, 45, -90), target(0, 0, 0);
    jCamera::SetCamera(camera, position, target, position + Vector::UpVector,
        DegreeToRadian(60), .1f, 10000, SCR_WIDTH, SCR_HEIGHT, true,
        (target - position).Length());
    camera->UpdateCamera();
    bool selected = false;
    for (const auto& [id, entity] : g_Editor->GetDocument().Entities())
        if (entity.Kind == jEditorAuthoring::EntityKind::Cube && entity.Material.OverrideEnabled)
        {
            g_Editor->SelectEntity(id); selected = true;
            std::cout << "Manual selected actual Cube ID " << id << ": " << entity.Name << std::endl;
            break;
        }
    if (!selected) throw std::runtime_error("Demo has no actual colored authored cube");
    SetAuthoringWorkspaceVisible(true);
    if (ManualMode() == "tools") SetAuthoringIntegrationDrawHook(DrawManualTools);
    if (ManualMode() == "settings") jEditorProjectSettings::SetVisible(true);
    std::cout << "Manual capture mode " << ManualMode()
              << "; actual UI; 1280x900; global text scale 1.15; camera (0,45,-90) -> origin"
              << std::endl;
}
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fingerprints(directory):
    if not directory.exists():
        return {}
    result = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            result[str(path.relative_to(directory))] = "symlink:" + os.readlink(path)
        elif path.is_file():
            result[str(path.relative_to(directory))] = digest(path)
    return result


def owned(path):
    path = path.resolve()
    if not any(path == allowed or allowed in path.parents for allowed in OWNED_OUTPUTS):
        raise ValueError("Capture output must remain under an approved build/authoring/manual capture directory")
    return path


def replace_once(source, anchor, replacement):
    if source.count(anchor) != 1:
        raise RuntimeError("Production Main anchor drifted: " + anchor)
    return source.replace(anchor, replacement, 1)


def prepare(build, output):
    main = ROOT / "jEngine/Main.cpp"
    source_hash = digest(main)
    source = main.read_text(encoding="utf-8")
    source = replace_once(source, '#include "Tests/jEditorIntegrationTests.h"',
                          '#include "Tests/jEditorIntegrationTests.h"\n' + DRIVER)
    source = replace_once(source, "    std::string authoringError;",
                          "    PrepareManualCaptureState();\n    std::string authoringError;")
    source = replace_once(source, '    std::cout << "SurfelGI: "',
                          '    PrepareManualCaptureView();\n    std::cout << "SurfelGI: "')
    driver, obj = output / "ManualMain.cpp", output / "ManualMain.cpp.o"
    driver.write_text(source, encoding="utf-8")
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(item for item in entries if Path(item["file"]).resolve() == main)
    compile_args = list(entry.get("arguments") or shlex.split(entry["command"]))
    old_object = compile_args[compile_args.index("-o") + 1]
    compile_args[compile_args.index("-o") + 1] = str(obj)
    compile_args[compile_args.index("-c") + 1] = str(driver)
    for flag in ("-MF", "-MJ", "-MT"):
        if flag in compile_args:
            compile_args[compile_args.index(flag) + 1] = str(output / "ManualMain.deps")
    with (output / "compile.log").open("w") as log:
        subprocess.run(compile_args, cwd=entry["directory"], stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=180)
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "jEngine"], text=True)
    candidates = [shlex.split(line) for line in commands.strip().splitlines()]
    tokens = next(parts for parts in reversed(candidates)
                  if any(value.endswith("/jEngine/Main.cpp.o") for value in parts)
                  and "-o" in parts)
    start = next(i for i, token in enumerate(tokens)
                 if Path(token).name in ("c++", "clang++", "g++"))
    end = next((i for i in range(start, len(tokens)) if tokens[i] == "&&"), len(tokens))
    link = tokens[start:end]
    matches = [i for i, token in enumerate(link)
               if token == old_object or token.endswith("/jEngine/Main.cpp.o")]
    if len(matches) != 1:
        raise RuntimeError("Expected one production Main object in current link command")
    link[matches[0]] = str(obj)
    binary = output / "jEngine-manual-capture"
    link[link.index("-o") + 1] = str(binary)
    objects = [build / value for value in link if value.endswith((".o", ".a"))]
    signatures = {str(p): [p.stat().st_size, p.stat().st_mtime_ns] for p in objects}
    with (output / "link.log").open("w") as log:
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=180)
    if source_hash != digest(main) or any(
            signatures[str(p)] != [p.stat().st_size, p.stat().st_mtime_ns] for p in objects):
        raise RuntimeError("Shared sources/objects changed while preparing the disposable executable")
    manifest = {"source_main_sha256": source_hash, "driver_sha256": digest(driver),
                "binary_sha256": digest(binary), "build": str(build),
                "compile": compile_args, "link": link, "objects": signatures}
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return binary


def runtime_environment(args):
    env = {key: value for key, value in os.environ.items() if not key.startswith("JENGINE_")}
    if args.icd:
        env["VK_DRIVER_FILES"] = str(args.icd.resolve(strict=True))
        env.pop("VK_ICD_FILENAMES", None)
    if not env.get("VK_DRIVER_FILES") and not env.get("VK_ICD_FILENAMES"):
        raise ValueError("Pass --icd or an explicit VK_DRIVER_FILES/VK_ICD_FILENAMES runtime")
    env.update(VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation", MTL_DEBUG_LAYER="1",
               JENGINE_MAX_FRAMES="120", JENGINE_FIXED_TIMESTEP="0.016666667",
               JENGINE_TEST_WAIT_FOR_SCENE="1")
    if args.experimental:
        env["MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING"] = "1"
    return env


def capture(args, output, binary):
    if sys.platform != "darwin":
        raise RuntimeError("The engine's frame-60 PNG capture path currently requires macOS Vulkan")
    manifest = json.loads((output / "build-manifest.json").read_text())
    if digest(binary) != manifest["binary_sha256"]:
        raise RuntimeError("Prepared executable does not match its build manifest")
    if digest(ROOT / "jEngine/Main.cpp") != manifest["source_main_sha256"] or any(
            [Path(path).stat().st_size, Path(path).stat().st_mtime_ns] != signature
            for path, signature in manifest["objects"].items()):
        raise RuntimeError("Shared Main/objects changed; prepare a current executable before capturing")
    project_file = args.project.resolve(strict=True)
    source = project_file.parent
    if any(path.is_symlink() for path in source.rglob("*")):
        raise RuntimeError("Use a self-contained demo without symlinked project inputs")
    demo_before = fingerprints(source)
    ini = ROOT / "jEngine/jengine.ini"
    ini_before = ini.read_bytes() if ini.exists() else None
    prefs = ROOT / "jEngine/.jeditor"
    prefs_before = fingerprints(prefs)
    env = runtime_environment(args)
    previous_report = output / "capture-info.json"
    if previous_report.exists():
        previous_report.rename(output / ("capture-info.previous-" + str(time.time_ns()) + ".json"))
    report = {"success": False, "project_source": str(project_file),
              "project_source_hashes": demo_before, "source_main_sha256": manifest["source_main_sha256"],
              "binary_sha256": manifest["binary_sha256"], "modes": {},
              "capture_frame": 60, "frames": 120, "text_scale": 1.15,
              "runtime": {k: env.get(k) for k in ("VK_DRIVER_FILES", "VK_ICD_FILENAMES",
                         "MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING", "VK_INSTANCE_LAYERS", "MTL_DEBUG_LAYER")}}
    awake = None
    try:
        if args.wake_display:
            subprocess.run(["/usr/bin/caffeinate", "-u", "-t", "1"], check=True)
            awake = subprocess.Popen(["/usr/bin/caffeinate", "-d", "-t", "600"])
        for mode in args.mode or MODES:
            destination = output / mode
            if destination.exists():
                destination.rename(output / (mode + ".previous-" + str(time.time_ns())))
            runtime = destination / "runtime"
            runtime.mkdir(parents=True)
            project = runtime / "project"
            shutil.copytree(source, project)
            if fingerprints(project) != demo_before:
                raise RuntimeError("Owned project copy differs from the source demo")
            (runtime / "Resource").symlink_to(ROOT / "jEngine/Resource", target_is_directory=True)
            state = destination / "workspace-state"
            state.mkdir()
            (state / "workspace.json").write_text(json.dumps({"fontScale": 1.0, "docking": True,
                "platformWindows": False, "autoSave": False}) + "\n")
            (state / "imgui.ini").write_text("[Window][Project Settings]\nPos=300,45\nSize=650,805\nCollapsed=0\n")
            image = destination / (mode + "-raw.png")
            case_env = {**env, "JENGINE_CAPTURE_FRAME": str(image),
                        "JENGINE_LOG_FILE": str(destination / "engine.jsonl"),
                        "JENGINE_MANUAL_MODE": mode, "JENGINE_MANUAL_STATE": str(state)}
            command = [str(binary), "-vulkan", "--project", str(project / project_file.name)]
            log_path = destination / "run.log"
            with log_path.open("w") as log:
                result = subprocess.run(command, cwd=runtime, env=case_env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=300)
            text = log_path.read_text(errors="replace")
            problems = PROBLEMS.findall(text)
            dimensions = None
            if image.exists():
                png = image.read_bytes()
                if png[:8] == b"\x89PNG\r\n\x1a\n":
                    dimensions = list(struct.unpack_from(">II", png, 16))
            entry = {"exit_code": result.returncode, "command": command, "log": str(log_path),
                     "raw_png": str(image), "dimensions": dimensions, "validation_problems": problems,
                     "clean_shutdown": "jEngine clean shutdown; rendered frames: 120" in text,
                     "selected_cube": re.findall(r"Manual selected actual Cube.*", text),
                     "runtime_project_changes": [name for name, value in fingerprints(project).items()
                                                 if demo_before.get(name) != value],
                     "image_sha256": digest(image) if image.exists() else None}
            report["modes"][mode] = entry
            print(json.dumps({"mode": mode, **entry}), flush=True)
            if (result.returncode or problems or dimensions != [1280, 900]
                    or not entry["clean_shutdown"] or not entry["selected_cube"]
                    or "Captured rendered frame:" not in text):
                raise RuntimeError("Actual capture failed: " + str(log_path))
        report["success"] = True
    finally:
        if awake:
            awake.terminate()
            awake.wait(timeout=10)
        report["preferences_preserved"] = (
            (ini.read_bytes() if ini.exists() else None) == ini_before
            and fingerprints(prefs) == prefs_before)
        report["source_demo_preserved"] = fingerprints(source) == demo_before
        (output / "capture-info.json").write_text(json.dumps(report, indent=2) + "\n")
        if not report["preferences_preserved"] or not report["source_demo_preserved"]:
            raise RuntimeError("Source preferences/demo changed during capture; inspect provenance")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/macos-debug")
    parser.add_argument("--output", type=Path, default=OWNED)
    parser.add_argument("--project", type=Path, default=ROOT / "examples/authoring-demo/project.jproject")
    parser.add_argument("--mode", action="append", choices=MODES)
    parser.add_argument("--icd", type=Path)
    parser.add_argument("--experimental", action="store_true")
    parser.add_argument("--wake-display", action="store_true")
    parser.add_argument("--no-build", action="store_true", help="Reuse the verified prepared executable")
    parser.add_argument("--run", action="store_true", help="Launch real GPU captures only after the lane is approved")
    args = parser.parse_args()
    output = owned(args.output)
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "jEngine-manual-capture"
    if not args.no_build:
        binary = prepare(args.build_dir.resolve(strict=True), output)
    if args.run:
        capture(args, output, binary)
    else:
        print(json.dumps({"prepared": str(binary), "gpu_launched": False,
                          "build_manifest": str(output / "build-manifest.json")}), flush=True)


if __name__ == "__main__":
    main()
