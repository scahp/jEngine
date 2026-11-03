# From rendering prototype to a usable personal engine

jEngine already has substantial rendering work: two graphics APIs, material and
mesh rendering, scene cameras/lights, postprocessing, hardware ray queries and
SurfelGI. The next milestone is a complete authoring loop: create a project,
import assets, place and edit objects, save, reopen, run and package a small game
without changing engine source for each scene.

This roadmap targets a small 3D engine for one developer. A feature's presence in
a commercial engine is not, by itself, a reason to implement it here.

## Milestones and acceptance criteria

| Priority | Work | Completion criterion |
| --- | --- | --- |
| Foundation, this change | Bounded asynchronous logging, CPU asset preparation with owner-thread publication, placement UI and repeatable benchmarks | Worker errors reach a bounded console and rotating file; Model import requests can fail/cancel without clearing the current scene; no worker mutates scene/GPU state; measured regressions pass. |
| Next: reliable scene editing | Stable entity/asset IDs, scene hierarchy and parent transforms, scene save/load with schema versions, explicit edit commands and undo/redo | Import and place 100 mixed instances, edit/reparent/duplicate/delete, undo/redo, save and reopen with the same IDs/materials/transforms. |
| Next: asset workflow | Asset registry and browser, source/import settings, dependency graph, shared mesh/material instances, thumbnails, import cache and safe reference-counted eviction | Reuse one mesh in 1,000 instances without decoding/uploading it 1,000 times; modified sources invalidate only affected imports; memory has a measured bound. |
| Next: upload and failure handling | Graphics-queue upload batches, byte budgets, completion tickets, staging retirement and backend allocation-error propagation | Large imports publish only after completed uploads; cancellation retires submitted work by fence; a budget/allocation failure reports an error without dereferencing a null resource. |
| Next: runtime boundary | Project settings, runtime/editor targets, play/stop state restoration, application lifecycle and input actions | Play mode starts from a scene copy, stop restores editing state; a packaged runtime opens the saved scene without editor panels or source-tree working-directory assumptions. |
| Game-specific runtime | Minimal collision/physics, audio, animation, gameplay components and serialization as required by the first sample game | One small game can be authored and packaged with automated load/play/shutdown checks on Windows and macOS. Select maintained libraries before implementing these systems. |
| Later: tooling scale | Docking/viewports, material tools, profiler timeline, hot reload and import cooking | Each feature has its own platform/lifetime tests and demonstrated authoring benefit. Parallel rendering or a larger job system requires profiling evidence first. |

Stable IDs, commands, serialization and resource ownership precede arbitrary
scene duplication and general undo. A pointer/index copied into an undo record
does not provide a safe lifetime model.

## Editor layout

Use the existing ImGui and ImGuizmo first. A docking upgrade also changes backend
and window lifecycle behavior; it is a separate, testable migration.

| Area | Purpose |
| --- | --- |
| Center | Scene viewport, selection outline, move/rotate/scale gizmo, local/world modes and explicit snapping. Camera input is suppressed while typing in UI. |
| Left | Searchable scene hierarchy with clipped rows, stable selection and an Add action. Parent/child hierarchy arrives with stable scene IDs. |
| Right | Selection inspector with typed transform/material/light properties, units and validation. Editing a field creates one undoable command in the next milestone. |
| Bottom | Asset browser/import queue and bounded searchable logs. Failed Model imports retain the current scene and expose a useful error. |
| Top | Project/scene/save, play/stop and build actions. Add these as their underlying workflows become functional. |

The current placement panel can provide compact **Add**, **Scene** and
**Inspector** sections without requiring a new window system. Search and clipping
must avoid formatting and submitting thousands of hidden rows. Multi-selection,
arbitrary duplicate, group pivots, undo and general scene saving remain explicit
follow-up work rather than partially working buttons.

## Asset/thread ownership contract

1. A request copies its path/settings and receives an ID. Queue capacity and
   completed CPU payload count are bounded. The scene browser uses the latest
   requested scene; superseded results cannot publish themselves.
2. CPU workers perform file reading, Assimp parsing, image decoding and stream
   preparation. They own their intermediate data. Per-key in-flight decoding
   shares results; a failed request can be retried.
3. The scene/render owner thread polls completions. It creates GPU resources and
   scene objects, then publishes a successful scene. Workers never call the RHI
   or register global scene objects.
4. Cancellation is cooperative between phases; an active library parse/decode
   cannot be forcibly interrupted. Shutdown stops admission, cancels and joins
   workers before releasing scene/GPU infrastructure.
5. Initial GPU finalization remains synchronous and limited by request count,
   not a guaranteed millisecond budget. The next step is staged uploads with
   byte/time budgets and fence-based publication, after upload ownership is explicit.

The legacy PathTracing importer remains synchronous and clears the previous scene before loading; its failure path is not transactional. The new retention/cancellation guarantee applies to the Model importer only.

The existing renderer keeps raw texture references backed by caches. Evicting
those cache entries would create dangling references. A real asset handle and
resource ownership migration is required before promising a hard total-memory
budget; a bounded import queue alone does not provide that guarantee.
Owner-thread finalization checks the resulting model buffers, but lower-level
backends do not yet propagate every allocation/device failure safely. Device loss
or arbitrary GPU out-of-memory recovery is not guaranteed by this change.

## Bottlenecks and measurement

The audit identified a macOS image-cache lock spanning decoding, console locking
across UI drawing, unbounded console retention, all-row placement UI work, and
synchronous scene import/upload. The foundation change addresses these concrete
paths. Existing per-resource GPU waits, shader compilation and path-tracing
import/publication need separate traces before changing their synchronization.
The shader watcher's cross-thread stop flag is now atomic, and Vulkan texture
creation avoids one unused staging allocation and full-image copy. Actual texture
upload/mip waits and publication/acceleration-structure completion waits remain.
Vulkan mesh copy submissions are asynchronous; they must not be reported as a
queue-idle wait per mesh. A future batching path should return explicit completion
tickets and initially use the graphics queue before extending queue-family ownership.

Record CPU preparation, queue wait, owner-thread upload/publish, frame time,
peak staging memory and cancellations separately. Compare the same assets,
build configuration, driver, resolution and camera; distinguish CPU-only
microbenchmarks from actual rendered-frame measurements. Keep raw samples and
report median/tail latency, including regressions and unchanged results.
