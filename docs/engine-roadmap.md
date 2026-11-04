# jEngine authoring and runtime roadmap

The current editor implements a project-to-runtime workflow: create a project,
import assets, edit a hierarchy, save and reopen a scene, preview it with Play,
and export a runtime folder. See the [authoring guide](editor-authoring.md),
[Korean manual](manual/README.md) and [verification report](reports/editor-authoring.md)
for supported behavior and measured limits.

## Implemented editor foundation

| Area | Current behavior |
| --- | --- |
| Persistence and commands | Versioned `.jscene`, stable IDs, parent transforms, atomic saves/backups, autosave recovery, bounded Undo/Redo, transactions, Prefabs and multi-root clipboard operations. |
| Workspace | Docked Hierarchy/Scene/Inspector/Project tools, optional native platform windows, saved layouts, multiselection and shared-pivot gizmos, guarded camera and keyboard input. |
| Assets | Stable registry IDs and sidecars, dependency scans, folder/search/PNG previews, drag-to-scene, import settings, cancel/reimport, shared geometry and material/texture leases. |
| Uploads | Byte/chunk/admission budgets, completion tickets, staged publication and cancellation retirement on the render owner thread. |
| Authoring tools | PBR Inspector, bounded material graph, transform timeline, CPU billboard particles, terrain heightfields/brushes, selected-terrain Recast/Detour bake and path queries. |
| Runtime and extensions | Play/Stop restoration, authored cameras, validated project settings, staged runtime export, versioned C ABI plugins and compiled-library reload/state migration. |
| Diagnostics | Filtered bounded console, rotating JSON logs, import/resource counters, editor diagnostics and reproducible tests. |

These implementations have defined scopes. Material graphs do not compile an
arbitrary shading language; animation covers transforms rather than skeletons;
particles use CPU simulation and raster rendering; navigation queries selected
terrain rather than a complete game world. Packaging targets the host OS and
architecture. See the guide before treating a feature name as a broader contract.

## Next engine milestones

| Priority | Work | Acceptance criterion |
| --- | --- | --- |
| First sample game | Choose a small game and implement only its gameplay components, lifecycle and input actions. | Author, save, run and package a complete playable loop without editing engine source for each scene. |
| Resident resource budgets | Handles and dependency-aware eviction across legacy and authored resources; streaming and measured RAM/VRAM ceilings. | Reuse one model in 1,000 instances, reimport safely while drawing, and prove bounded memory after repeated load/unload cycles. |
| Remaining owner-thread stalls | Trace shader compilation, allocations, material texture upload, thumbnails, terrain rebuilds and acceleration structures. | Publish large imports without hiding long driver/AS work behind a byte-budget claim; record per-phase and tail latency. |
| Gameplay services | Select maintained physics/collision and audio libraries; add game-specific scripting/components and serialization. | An actual sample uses collision, sound and reproducible play/load/shutdown checks on Windows and macOS. |
| Broader authoring | Skeletal animation/events, richer graph compilation, model-render thumbnails, viewport terrain painting, navigation overlays/agents. | Each feature changes the actual runtime result and includes persistence, Undo, lifetime and platform tests. |
| Deployment quality | Installers, platform signing/notarization, update and crash-recovery workflows. | A clean user machine runs an exported sample and exposes actionable startup diagnostics. |

## Thread and resource ownership

1. Requests copy paths/settings and receive IDs/generations. Queues and ready
   payload counts are bounded; superseded results cannot publish themselves.
2. CPU workers own file reading, import parsing, decoding and plain preparation
   results. They never mutate scene registries or submit RHI work.
3. The render owner admits upload chunks, tracks completion tickets and publishes
   finished model instances. Cancellation retires submitted resources by completion.
4. Library parsing/decoding is cooperatively cancellable between phases. Shutdown
   cancels and joins workers before destroying the scene/GPU infrastructure.
5. Shared geometry and material/texture leases keep old revisions alive while
   consumers reference them. Legacy raw-reference caches still require migration
   before promising a hard total-memory ceiling.

A byte budget limits copy work, and an admission timer decides whether to start
another chunk. Neither preempts a driver allocation, shader compilation or AS
build that is already running. Legacy PathTracing scene import remains synchronous
and its replacement path has different failure behavior from authored models.
Arbitrary device-loss/out-of-memory recovery is not guaranteed.

## Measurement contract

Record CPU preparation, queue wait, upload admission, submission/completion,
scene/AS publication, steady frame time, staging memory and cancellation separately.
Use the same assets, build configuration, driver, resolution, camera and temporal
history for before/after comparisons. Report new-feature overhead and regressions
alongside improvements; CPU-only or cached-list gains do not establish an engine
FPS improvement.
