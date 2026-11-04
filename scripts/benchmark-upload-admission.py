#!/usr/bin/env python3
"""Measure actual Release model upload admission with a disposable Main.

No engine source, normal build object, INI or workspace preference is edited.
--prepare-only writes the driver/empty project. --build-only compiles/links only
the copied Main against an already completed Release build. GPU execution needs
the explicit --run flag; run it only while other GPU tests/benchmarks are idle.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import sys
import time
import uuid


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "build/authoring/upload-benchmark"
INCLUDES = r'''
#include "FileLoader/jModelLoader.h"
#include "FileLoader/jModelUploadSession.h"
#include "FileLoader/jImageFileLoader.h"
#include "FileLoader/jEditorAssetPath.h"
#include "Scene/jMeshObject.h"
#include "Scene/jObject.h"
#include "RHI/jRaytracingScene.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <cmath>
#include <fstream>
#include <future>
'''

BENCHMARK = r'''
// Disposable benchmark hook; production Main is never changed.
static int RunUploadAdmissionBenchmark()
{
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    using Json = nlohmann::json;
    using jEditorAssetPath::FromUtf8;
    using jEditorAssetPath::ToUtf8;
    const auto ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    const char* reportName = std::getenv("JENGINE_UPLOAD_REPORT");
    const char* sourceName = std::getenv("JENGINE_UPLOAD_MODEL");
    if (!reportName || !sourceName) { std::cerr << "Benchmark requires report/model paths" << std::endl; return 1; }
    const auto reportPath = FromUtf8(reportName);
    Json result{{"schemaVersion", 1}, {"status", "failed"}, {"model", sourceName},
        {"scope", "CPU preparation on worker; owner BeginFinalize and Step until ready; TakeModel and world registration separately"},
        {"limitations", {"No rendered frames or acceleration structures are built by this scenario.",
            "Step wall time includes owner CPU/driver work, not GPU timestamps.",
            "Upload wall time includes measured 1 ms harness polling pauses.",
            "Fresh process means cold engine image/texture caches, not a flushed filesystem or driver cache."}}};
    std::unique_ptr<jModelUploadSession> upload;
    std::unique_ptr<jMeshObject> object;
    bool registered = false;
    try
    {
        auto& loader = jModelLoader::GetInstance(); // Establish owner before launching preparation.
        auto& images = jImageFileLoader::GetInstance();
        if (!loader.IsOwnerThread()) throw std::runtime_error("Benchmark started on the wrong upload owner");
        if (g_Editor && !g_Editor->GetDocument().Entities().empty()) throw std::runtime_error("Benchmark requires an empty startup document");
        const auto beforeImages = images.GetStats();
        const auto source = fs::canonical(FromUtf8(sourceName));
        const std::string modelPath = ToUtf8(source), materialRoot = ToUtf8(source.parent_path());
        struct Prepared
        {
            std::unique_ptr<jPreparedModel> Model;
            std::string Error;
            double Milliseconds = 0;
            bool RanOnOwner = true;
        };
        const auto preparationStarted = Clock::now();
        auto future = std::async(std::launch::async, [&loader, modelPath, materialRoot] {
            Prepared prepared;
            prepared.RanOnOwner = loader.IsOwnerThread();
            const auto started = Clock::now();
            prepared.Model = loader.PrepareModel(modelPath.c_str(), materialRoot.c_str(), {}, &prepared.Error);
            prepared.Milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            return prepared;
        });
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto prepared = future.get();
        const double preparationWall = ms(Clock::now() - preparationStarted);
        result["cpu_prepare_worker_ms"] = prepared.Milliseconds;
        result["cpu_prepare_future_wall_ms"] = preparationWall;
        result["preparation_ran_on_owner"] = prepared.RanOnOwner;
        if (!prepared.Model) throw std::runtime_error("PrepareModel: " + prepared.Error);
        if (prepared.RanOnOwner) throw std::runtime_error("CPU preparation incorrectly ran on owner");
        const auto stats = prepared.Model->GetStats();
        const auto afterImages = images.GetStats();
        result["model_stats"] = {{"vertices", stats.VertexCount}, {"indices", stats.IndexCount},
            {"submeshes", stats.SubMeshCount}, {"textures", stats.TextureCount}, {"reported_cpu_ms", stats.CpuMilliseconds}};
        result["engine_image_cache"] = {{"before_cached", beforeImages.Cached}, {"after_cached", afterImages.Cached},
            {"decode_attempts_delta", afterImages.DecodeAttempts - beforeImages.DecodeAttempts},
            {"cache_hits_delta", afterImages.CacheHits - beforeImages.CacheHits},
            {"coalesced_waits_delta", afterImages.CoalescedWaits - beforeImages.CoalescedWaits}};
        if (stats.VertexCount != 192496 || stats.TextureCount != 68)
            throw std::runtime_error("The Sponza fixture no longer matches the expected 192496 vertices / 68 textures");
        if (afterImages.DecodeAttempts - beforeImages.DecodeAttempts != stats.TextureCount)
            throw std::runtime_error("Sponza image decode cache was not cold for every prepared image");
        std::string error;
        const auto beginStarted = Clock::now();
        upload = loader.BeginFinalize(std::move(prepared.Model), &error);
        result["begin_finalize_wall_ms"] = ms(Clock::now() - beginStarted);
        if (!upload) throw std::runtime_error("BeginFinalize: " + error);
        const jModelUploadSession::Budget budget; // Actual production defaults, never overridden.
        result["budget"] = {{"max_bytes", budget.MaxBytes}, {"admission_ms", budget.MaxAdmissionMilliseconds},
            {"max_pending_chunks", budget.MaxPendingChunks}, {"harness_poll_pause_requested_ms", 1}};
        result["steps"] = Json::array();
        std::vector<double> wallTimes, admissionTimes;
        uint64_t maximumBytes = 0;
        size_t maximumTickets = 0, admissionOverruns = 0, externalOverruns = 0;
        double pollingWall = 0;
        const auto uploadStarted = Clock::now();
        const auto deadline = uploadStarted + std::chrono::seconds(180);
        jModelUploadSession::Progress progress;
        for (size_t index = 0;; ++index)
        {
            const auto started = Clock::now();
            progress = upload->Step(budget);
            const double wall = ms(Clock::now() - started);
            wallTimes.push_back(wall); admissionTimes.push_back(progress.LastStepMilliseconds);
            maximumBytes = std::max(maximumBytes, progress.LastStepBytes);
            maximumTickets = std::max(maximumTickets, progress.PendingChunks);
            admissionOverruns += progress.AdmissionOverrun;
            externalOverruns += wall > budget.MaxAdmissionMilliseconds;
            result["steps"].push_back({{"index", index}, {"call_wall_ms", wall},
                {"reported_admission_ms", progress.LastStepMilliseconds}, {"reported_overrun", progress.AdmissionOverrun},
                {"last_bytes", progress.LastStepBytes}, {"submitted_bytes", progress.SubmittedBytes},
                {"completed_bytes", progress.CompletedBytes}, {"pending_chunks", progress.PendingChunks},
                {"state", int(progress.Current)}});
            if (progress.LastStepBytes > budget.MaxBytes || progress.PendingChunks > budget.MaxPendingChunks)
                throw std::runtime_error("Upload exceeded its byte or in-flight ticket budget");
            if (progress.Current == jModelUploadSession::State::Failed) throw std::runtime_error(progress.Error);
            if (progress.Current == jModelUploadSession::State::Ready) break;
            if (Clock::now() >= deadline || index >= 100000) throw std::runtime_error("Sponza upload timed out");
            const auto pauseStarted = Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            pollingWall += ms(Clock::now() - pauseStarted);
        }
        result["upload_wall_ms"] = ms(Clock::now() - uploadStarted);
        result["poll_pause_wall_ms"] = pollingWall;
        const auto summarize = [](std::vector<double> values) -> Json {
            std::sort(values.begin(), values.end());
            double sum = 0; for (double value : values) sum += value;
            const size_t n = values.size();
            const size_t rank = std::min(n - 1, size_t(std::ceil(.95 * n)) - 1);
            const double median = n % 2 ? values[n / 2] : .5 * (values[n / 2 - 1] + values[n / 2]);
            return {{"count", n}, {"sum_ms", sum}, {"min_ms", values.front()}, {"median_ms", median},
                {"p95_ms", values[rank]}, {"max_ms", values.back()}};
        };
        result["step_call_wall"] = summarize(wallTimes);
        result["reported_admission"] = summarize(admissionTimes);
        result["harness_bookkeeping_wall_ms"] = std::max(0.0, result["upload_wall_ms"].get<double>()
            - pollingWall - result["step_call_wall"]["sum_ms"].get<double>());
        result["reported_admission_overruns"] = admissionOverruns;
        result["external_step_call_over_2ms"] = externalOverruns;
        result["last_reported_admission_ms"] = progress.LastStepMilliseconds;
        result["worst_reported_admission_ms"] = progress.WorstStepMilliseconds;
        result["actual_max_step_bytes"] = maximumBytes;
        result["actual_max_pending_chunks"] = maximumTickets;
        result["upload_bytes"] = {{"total", progress.TotalBytes}, {"submitted", progress.SubmittedBytes}, {"completed", progress.CompletedBytes}};
        if (progress.CompletedBytes != progress.TotalBytes) throw std::runtime_error("Ready upload has incomplete byte accounting");
        const auto takeStarted = Clock::now();
        object = upload->TakeModel();
        result["take_model_wall_ms"] = ms(Clock::now() - takeStarted);
        if (!object || object->RenderObjects.empty()) throw std::runtime_error("TakeModel did not publish a complete model");
        const auto objectsBefore = jObject::GetStaticObject().size();
        const auto meshesBefore = jObject::GetStaticRenderObject().size();
        const auto publishStarted = Clock::now();
        jObject::AddObject(object.get()); registered = true;
        result["world_register_wall_ms"] = ms(Clock::now() - publishStarted);
        result["world_registration"] = {{"objects_before", objectsBefore}, {"objects_after", jObject::GetStaticObject().size()},
            {"meshes_before", meshesBefore}, {"meshes_after", jObject::GetStaticRenderObject().size()},
            {"model_meshes", object->RenderObjects.size()}, {"as_build_executed", false}, {"rendered_frames", 0}};
        if (jObject::GetStaticObject().size() != objectsBefore + 1
            || jObject::GetStaticRenderObject().size() != meshesBefore + object->RenderObjects.size())
            throw std::runtime_error("World registration did not expose all completed model meshes");
        result["status"] = "passed";
    }
    catch (const std::exception& exception) { result["error"] = exception.what(); }
    // Cleanup and shutdown are intentionally outside all measured stages.
    if (registered) jObject::RemoveObject(object.get());
    if (upload) upload->Cancel();
    jModelUploadSession::DrainRetired();
    object.reset(); upload.reset();
    g_rhi->Flush();
    try
    {
        std::ofstream output(reportPath, std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit); output << result.dump(2) << '\n';
    }
    catch (const std::exception& exception) { std::cerr << exception.what() << std::endl; return 1; }
    std::cout << "UPLOAD_BENCHMARK " << result["status"] << " report=" << ToUtf8(reportPath) << std::endl;
    return result["status"] == "passed" ? 0 : 1;
}

'''


def digest(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def prepare(out: Path) -> tuple[Path, Path]:
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "jEngine/Main.cpp").read_text()
    anchor = '#include "Core/jLog.h"'
    assert source.count(anchor) == 1
    source = source.replace(anchor, anchor + "\n" + INCLUDES, 1)
    anchor = "int RunCpuSelfTests();"
    assert source.count(anchor) == 1
    source = source.replace(anchor, BENCHMARK + anchor, 1)
    anchor = "const int failures = RunModelUploadGpuTests() + RunEditorParticleMaterialGpuTests();"
    assert source.count(anchor) == 1, "Production GPU hook changed; inspect before replacing it"
    source = source.replace(anchor, "const int failures = RunUploadAdmissionBenchmark();", 1)
    anchor = "    jLogConfig logConfig;"
    assert source.count(anchor) == 1
    source = source.replace(anchor, r'''    if (const char* report = std::getenv("JENGINE_UPLOAD_REPORT"))
        SetAuthoringTestStateDirectory(jEditorAssetPath::FromUtf8(report).parent_path() / "workspace-state");
''' + anchor, 1)
    main = out / "UploadBenchmarkMain.cpp"
    main.write_text(source)
    project = out / "empty-project/project.jproject"
    write_json(project, {"schemaVersion": 1, "projectId": str(uuid.uuid4()),
        "name": "Disposable upload benchmark", "assetRoot": "Assets", "startupScene": "Scenes/start.jscene",
        "settings": {"render.deferred": "true", "render.vsync": "false"}})
    (project.parent / "Assets").mkdir(exist_ok=True)
    write_json(project.parent / "Scenes/start.jscene", {"format": "jEngineScene", "version": 1, "name": "Empty", "entities": []})
    return main, project


def compile_driver(build: Path, out: Path, main: Path) -> tuple[Path, dict]:
    if "CMAKE_BUILD_TYPE:STRING=Release" not in (build / "CMakeCache.txt").read_text():
        raise RuntimeError("Use a completed single-config Release build")
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(item for item in entries if Path(item["file"]).resolve() == ROOT / "jEngine/Main.cpp")
    args = shlex.split(entry["command"])
    obj = out / "UploadBenchmarkMain.cpp.o"
    args[args.index("-o") + 1] = str(obj)
    args[args.index("-c") + 1] = str(main)
    # Never overwrite the normal Main's compiler dependency records either.
    stripped, index = [], 0
    while index < len(args):
        if args[index] in ("-MF", "-MT", "-MQ"):
            index += 2
        elif args[index] in ("-MD", "-MMD"):
            index += 1
        else:
            stripped.append(args[index]); index += 1
    with (out / "compile.log").open("w") as log:
        subprocess.run(stripped, cwd=entry["directory"], stdout=log, stderr=subprocess.STDOUT, check=True)
    commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "jEngine"], text=True)
    link = shlex.split(commands.strip().splitlines()[-1])
    if link[:2] == [":", "&&"]:
        link = link[2:]
    if link[-2:] == ["&&", ":"]:
        link = link[:-2]
    if any(token in link for token in ("&&", ";", "||")):
        raise RuntimeError("Unexpected compound link command; inspect instead of running side effects")
    old_obj = "CMakeFiles/jEngine.dir/jEngine/Main.cpp.o"
    assert link.count(old_obj) == 1
    link[link.index(old_obj)] = str(obj)
    linked_objects = [build / token for token in link if token.endswith(".o")]
    binary = out / "jEngine-upload-benchmark"
    link[link.index("-o") + 1] = str(binary)
    with (out / "link.log").open("w") as log:
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    metadata = {"configuration": "Release", "compile_command": stripped, "link_command": link,
        "repository_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "normal_main_unchanged_sha256": digest(ROOT / "jEngine/Main.cpp"), "driver_sha256": digest(main),
        "binary_sha256": digest(binary), "linked_objects": len(linked_objects),
        "newest_linked_object_mtime_ns": max(path.stat().st_mtime_ns for path in linked_objects)}
    write_json(out / "driver-build.json", metadata)
    return binary, metadata


def run_benchmark(args, build: Path, out: Path, project: Path, binary: Path, metadata: dict) -> None:
    icd = args.icd or os.environ.get("VK_DRIVER_FILES")
    if not icd or not Path(icd).is_file():
        raise RuntimeError("--run requires --icd <MoltenVK_icd.json> or a valid VK_DRIVER_FILES")
    model = (ROOT / "jEngine/Resource/sponza_pbr/sponza.glb").resolve()
    protected = [ROOT / "jEngine/Main.cpp", ROOT / "jEngine/jengine.ini", ROOT / "jEngine/imgui.ini",
        ROOT / "jEngine/.jeditor/workspace.json", ROOT / "jEngine/.jeditor/imgui.ini", ROOT / "jEngine/.jeditor/recent-projects.json"]
    before = {str(path): digest(path) for path in protected}
    reports = []
    started = time.time()
    invocation = out / "runs" / str(time.time_ns())
    try:
        for index in range(1, args.runs + 1):
            run = invocation / f"run-{index}"
            run.mkdir(parents=True, exist_ok=True)
            runtime = run / "runtime"
            runtime.mkdir(exist_ok=True)
            resource = runtime / "Resource"
            if resource.is_symlink():
                if resource.resolve() != ROOT / "jEngine/Resource":
                    raise RuntimeError("Runtime Resource symlink points somewhere unexpected")
            elif resource.exists():
                raise RuntimeError("Runtime Resource must be an owned symlink")
            else:
                resource.symlink_to(ROOT / "jEngine/Resource", target_is_directory=True)
            (runtime / "jengine.ini").write_text("[UI]\nShowEditorUI=0\n[SceneBrowser]\nLastLoadedSceneId=\n")
            env = dict(os.environ)
            for key in list(env):
                if key.startswith(("JENGINE_TEST_", "JENGINE_CAPTURE_")):
                    env.pop(key)
            if not args.validation:
                env.pop("VK_INSTANCE_LAYERS", None)
                env["MTL_DEBUG_LAYER"] = "0"
            else:
                env["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
                env["MTL_DEBUG_LAYER"] = "1"
            env.update(VK_DRIVER_FILES=str(Path(icd).resolve()), MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING="1",
                JENGINE_MAX_FRAMES="1", JENGINE_FIXED_TIMESTEP="0.016666667", JENGINE_SURFEL_GI="0", JENGINE_INLINE_RAY="0",
                JENGINE_UPLOAD_REPORT=str(run / "report.json"), JENGINE_UPLOAD_MODEL=str(model), JENGINE_LOG_FILE=str(run / "engine.jsonl"))
            with (run / "run.log").open("w") as log:
                process = subprocess.run([str(binary), "--ci-editor-gpu", "--project", str(project), "-vulkan"],
                    cwd=runtime, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            report = json.loads((run / "report.json").read_text())
            log_text = (run / "run.log").read_text(errors="replace")
            report["process"] = {"exit_code": process.returncode, "pid_isolated": True,
                "runtime_cwd": str(runtime), "validation_enabled": args.validation,
                "validation_errors": [line for line in log_text.splitlines() if "Validation Error" in line or "VUID-" in line]}
            write_json(run / "report.json", report)
            reports.append(report)
            print(json.dumps({"run": index, "status": report["status"], "cpu_prepare_ms": report.get("cpu_prepare_worker_ms"),
                "upload_wall_ms": report.get("upload_wall_ms"), "step_max_ms": report.get("step_call_wall", {}).get("max_ms"),
                "reported_overruns": report.get("reported_admission_overruns")}), flush=True)
            if process.returncode or report["status"] != "passed" or report["process"]["validation_errors"]:
                raise RuntimeError(f"Upload benchmark run {index} failed; inspect {run}")
    finally:
        after = {str(path): digest(path) for path in protected}
        write_json(out / "preservation.json", {"before": before, "after": after, "unchanged": before == after})
        if before != after:
            raise RuntimeError("An engine source/settings file changed during benchmarking")
    measures = {key: [report[key] for report in reports] for key in
        ("cpu_prepare_worker_ms", "cpu_prepare_future_wall_ms", "begin_finalize_wall_ms", "upload_wall_ms", "take_model_wall_ms", "world_register_wall_ms")}
    measures["step_max_ms"] = [report["step_call_wall"]["max_ms"] for report in reports]
    measures["step_p95_ms"] = [report["step_call_wall"]["p95_ms"] for report in reports]
    summary = {"schemaVersion": 1, "status": "passed", "runs": len(reports), "cold_engine_cache": True,
        "filesystem_cache_flushed": False, "configuration": "Release", "host": platform.platform(),
        "model": str(model), "model_sha256": digest(model), "build": metadata, "gpu_validation": args.validation,
        "wall_seconds_including_process_startup_and_shutdown": time.time() - started,
        "metrics": {key: {"runs": values, "median": statistics.median(values), "min": min(values), "max": max(values)} for key, values in measures.items()},
        "budgets": [report["budget"] for report in reports],
        "historical_reference": {"owner_pause_ms": 135.553, "scope_comparable": False,
            "source": "docs/reports/editor-foundation-performance.md:69",
            "note": "Historical pause included upload, scene replacement and acceleration-structure waits. This measures default chunk Step calls plus separate Begin/Take/world registration, with no scene replacement or AS build. Different scope/build/cache/environment; no percentage speedup or frame/AS claim."},
        "reports": [str(invocation / f"run-{index}/report.json") for index in range(1, args.runs + 1)]}
    write_json(out / "summary.json", summary)
    print(json.dumps({"summary": str(out / "summary.json"), "metrics": summary["metrics"]}), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-only", action="store_true")
    mode.add_argument("--build-only", action="store_true")
    mode.add_argument("--run", action="store_true", help="Explicitly allow GPU runs after compiling/linking the disposable driver")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/macos-release")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--icd", help="Explicit MoltenVK ICD path")
    parser.add_argument("--validation", action="store_true", help="Enable Vulkan/Metal validation; reported separately from default Release timings")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This disposable driver currently reuses macOS Release/Ninja objects")
    if not 1 <= args.runs <= 10:
        parser.error("--runs must be 1..10")
    out, build = args.output.resolve(), args.build_dir.resolve()
    main_source, project = prepare(out)
    if args.prepare_only:
        print(json.dumps({"prepared_main": str(main_source), "empty_project": str(project), "gpu_launched": False}))
        return
    binary, metadata = compile_driver(build, out, main_source)
    if args.run:
        run_benchmark(args, build, out, project, binary, metadata)
    else:
        print(json.dumps({"binary": str(binary), "gpu_launched": False}))


if __name__ == "__main__":
    main()
