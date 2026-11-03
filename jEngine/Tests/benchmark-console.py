#!/usr/bin/env python3
"""Build one real-console/ImGui driver against a pinned old and current console.

macOS runner (needs a configured Release compile_commands.json). Windows can run
the same compiled C++ entrypoint through --ci-log-benchmark. This script never
modifies the checkout/branch, user ini, engine lifecycle or renderer. Artifacts
include build commands, source hashes, complete timing samples and a summary.
"""

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import pathlib
import platform
import shlex
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, default=pathlib.Path("build/macos-release"))
    parser.add_argument("--baseline", default="ecb49a121773d2b066b47d95be218c547798d21c")
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("build/editor-foundation/logging-benchmark"))
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    build = (root / args.build_dir).resolve()
    output = (root / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    if platform.system() != "Darwin":
        parser.error("This standalone build runner uses the macOS Release compile database")
    baseline = subprocess.check_output(["git", "rev-parse", args.baseline + "^{commit}"], cwd=root, text=True).strip()
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(item for item in entries if item["file"].endswith("/ConsoleVariables/jConsole.cpp"))
    original = entry.get("arguments") or shlex.split(entry["command"])
    flags, index = [], 0
    while index < len(original):
        if original[index] == "-Xclang":
            index += 4  # PCH -include/-include-pch plus its argument pair.
        elif original[index] in ("-o", "-c"):
            index += 2
        elif original[index] == "-Winvalid-pch":
            index += 1
        else:
            flags.append(original[index])
            index += 1
    if not any(flag.startswith("-O") and flag != "-O0" for flag in flags):
        parser.error("Use a Release compile database for comparable timing")
    flags += ["-w", "-I" + str(root / "jEngine/External/spdlog/include"),
              "-I" + str(root / "jEngine/Code/Engine/ConsoleVariables")]
    commands = []

    def compile_unit(source, target, extra=()):
        command = flags[:1] + list(extra) + flags[1:] + ["-o", str(target), "-c", str(source)]
        commands.append(command)
        with (output / (target.name + ".build.log")).open("w") as log:
            subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        return target

    with tempfile.TemporaryDirectory(prefix="jengine-console-benchmark-") as temporary:
        temporary = pathlib.Path(temporary)
        old_include = temporary / "baseline"
        old_folder = old_include / "Code/Engine/ConsoleVariables"
        old_folder.mkdir(parents=True)
        for name in ("jConsole.cpp", "jConsole.h"):
            content = subprocess.check_output(["git", "show", baseline + ":jEngine/Code/Engine/ConsoleVariables/" + name], cwd=root)
            (old_folder / name).write_bytes(content)
        (temporary / "main.cpp").write_text("int RunLogBenchmark();\nint main() { return RunLogBenchmark(); }\n")
        jobs = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            imgui = []
            for name in ("imgui", "imgui_draw", "imgui_tables", "imgui_widgets"):
                target = temporary / (name + ".o")
                imgui.append(target)
                jobs.append(pool.submit(compile_unit, root / ("jEngine/External/IMGUI/" + name + ".cpp"), target))
            jobs.append(pool.submit(compile_unit, old_folder / "jConsole.cpp", temporary / "before-console.o", ("-I" + str(old_include),)))
            jobs.append(pool.submit(compile_unit, root / "jEngine/Code/Engine/ConsoleVariables/jConsole.cpp", temporary / "after-console.o"))
            jobs.append(pool.submit(compile_unit, root / "jEngine/Core/jLog.cpp", temporary / "jLog.o"))
            jobs.append(pool.submit(compile_unit, root / "jEngine/Tests/jLogBenchmark.cpp", temporary / "before-driver.o",
                                    ("-I" + str(old_include), "-DJENGINE_CONSOLE_BENCHMARK_BASELINE")))
            jobs.append(pool.submit(compile_unit, root / "jEngine/Tests/jLogBenchmark.cpp", temporary / "after-driver.o"))
            jobs.append(pool.submit(compile_unit, temporary / "main.cpp", temporary / "main.o"))
            for job in jobs:
                job.result()
        for phase in ("before", "after"):
            objects = imgui + [temporary / "main.o", temporary / (phase + "-console.o"), temporary / (phase + "-driver.o")]
            if phase == "after":
                objects.append(temporary / "jLog.o")
            executable = temporary / phase
            command = [flags[0], "-std=c++20", "-O3", "-arch", "arm64", "-o", str(executable)] + list(map(str, objects))
            commands.append(command)
            with (output / (phase + ".link.log")).open("w") as log:
                subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
            with (output / (phase + ".jsonl")).open("w") as log, (output / (phase + ".stderr.log")).open("w") as error:
                subprocess.run([str(executable)], cwd=root, stdout=log, stderr=error, check=True, timeout=120)
    (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    rows = {phase: [json.loads(line) for line in (output / (phase + ".jsonl")).read_text().splitlines()] for phase in ("before", "after")}
    summary = []
    for count in (1000, 10000, 50000):
        item = {"input_records": count}
        for phase in ("before", "after"):
            selected = [row for row in rows[phase] if row["benchmark"] == "console" and row["input_records"] == count]
            item[phase] = {name: statistics.median(row[name] for row in selected)
                           for name in ("render_median_ms", "render_p95_ms", "producer_p95_ms", "producer_p99_ms", "producer_max_ms")}
        item["render_median_ratio"] = item["before"]["render_median_ms"] / item["after"]["render_median_ms"]
        summary.append(item)
    source_paths = ["jEngine/Core/jLog.h", "jEngine/Core/jLog.cpp", "jEngine/Code/Engine/ConsoleVariables/jConsole.h",
                    "jEngine/Code/Engine/ConsoleVariables/jConsole.cpp", "jEngine/Tests/jLogBenchmark.cpp"]
    result = {"measured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "baseline_commit": baseline,
              "current_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "platform": platform.platform(), "machine": platform.machine(), "compiler": flags[0],
              "method": "Real console and ImGui; 1280x720; Release; 3x30 draws after 5 warmups; concurrent worker at 1ms / UI at 16ms; console comparison has no file sink",
              "source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in source_paths},
              "console": summary, "new_async_file_only": [row for row in rows["after"] if row["benchmark"] == "async_file"]}
    (output / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
