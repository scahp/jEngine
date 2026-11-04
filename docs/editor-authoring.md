# Editor authoring: capabilities and runtime contracts

The default editor workspace authors a versioned `.jscene` document and publishes it to the live engine scene. This guide describes the implemented paths and their boundaries. The [Korean user manual](manual/README.md) gives the step-by-step workflow. The five original annotated screenshots show the classic rendering panels opened with **F10**, rather than the new docked workspace.

## Feature and support matrix

| Area | Implemented capability | Runtime or workflow boundary |
| --- | --- | --- |
| Workspace | Docked Hierarchy, Scene/Game, Inspector and Project tools; saved layout/text scale; reset layout; optional separate OS windows | Separate windows require real monitor information plus platform/renderer callbacks. Vulkan also requires one graphics/present queue family. Sleeping/headless displays fall back to the main window until real monitor information returns. F10 switches rendering panels while authoring commands remain active. |
| Scene document | Stable 64-bit entity IDs; names, parent hierarchy, visibility/locking, typed transforms/components; version 1 JSON | Entity IDs are separate from transient object/picking IDs. Local transforms are TRS; inherited affine world matrices retain shear. A requested local transform that cannot be represented is rejected. |
| Persistence | New/Open/Save/Save As, dirty confirmation, atomic scene-file saves, previous `.bak`, recovery autosave | Models/textures remain project-relative external assets. Undo history, free editor camera pose and live particle simulation state are not scene snapshots. Recovery is a conditional 60-second edit-mode write, not a write after every input. |
| History and reuse | Undo/Redo, multi-root duplication/deletion, hierarchy drag reparent, selected-subtree Prefab save/instantiate, multi-root clipboard Copy/Paste | Clipboard roots preserve world TRS and deduplicate selected descendants. Nonrepresentable shear or animated external ancestors reject copy. Prefab instantiation creates new IDs; there is no linked-instance propagation system. Asset filesystem operations are outside scene Undo. |
| Runtime objects | Real cube/sphere, imported models, point/spot/directional lights and authored cameras | Models require reconstructible source references. Duplicates have independent object/render identities and share geometry/material handles; authored material changes use copy-on-write. |
| Selection and transforms | Stable single/multi selection, group pivot delta, parent-relative TRS, actual hide/show registration, focus/orbit/fly controls | Saved model mesh-local overrides are index-based. Count mismatch preserves the existing instance and reports an error; same-count mesh reorder still requires inspection. Non-TRS source capture is rejected rather than serialized as identity. |
| Project and assets | `project.jproject`, startup scene, recent projects; stable UUID `.jasset` metadata; dependency catalog; background scans; import/reimport; folders/rename/move/recoverable trash | Source paths stay inside the project. Moves/trash that break dependencies can be refused. Import file copies the selected source, not an arbitrary external dependency tree. Model source support depends on the existing Assimp/import path. |
| Asset previews | Demand-driven, revision-keyed PNG thumbnails with worker preparation and real UI textures | PNG image previews, not general 3D model render thumbnails. Other formats can display kind tiles. Thumbnail work/cache are bounded and defer GPU admission during model imports. |
| Import settings | Model geometry scale, V-coordinate flip and missing-normal generation, read from `.jasset` during real CPU preparation | Defaults are scale 1, flipUVs true, generateNormals true. Settings are not merely serialized UI values. Existing saved mesh-local overrides also affect the final result. |
| Project settings | Validated startup scene, renderer defaults and six editor-camera key bindings; Save and Apply/Revert/defaults | Renderer settings affect the existing raster path. Unsupported Inline/VSync requests are retained but not enabled effectively. This is not a general gameplay input system. |
| Materials | Actual per-instance PBR factors, base-color/normal textures, original imported-material preservation, reimport invalidation | Turning off imported overrides restores the source material. Opacity follows the existing raster/alpha-test path, not a new general order-independent transparency renderer. |
| Material graph | Color, Scalar, Texture, Multiply and Add nodes; pin connections; graph undo; `.jmaterial` Save and Apply | Constant arithmetic and one base-color texture multiplied by constants. No texture mixing/addition, texture Metallic/Roughness outputs or arbitrary shader language. Normal texture assignment is a separate Inspector slot. |
| Animation and Play | Local position/rotation/scale keyframes, loop/duration, Play/Stop snapshot, real timeline seek without document dirty/history changes; primary authored camera | Transform animation only. Stop discards Play edits and restores edit document/selection/camera. No skeletal animation/event tracks. Particle seeking is not exact arbitrary-time simulation reconstruction. |
| Terrain | Heightfield mesh, resolution/size, Raise/Lower/Flatten/Smooth brush, persisted heights and real geometry regeneration | Brush position/radius/strength are panel controls with Apply brush, not a viewport paint gesture. Resolution changes reset heights. Geometry regeneration can stall and is not covered by model-copy admission budgets. |
| Navigation | Recast/Detour bake and path query from the selected Terrain; local endpoints; polygon/path counts, coordinate preview and `.jnav` output | Selected terrain only; no whole-level geometry gathering, crowd/agent controller or automatic movement. Preview is a panel coordinate list. |
| Particles | Deterministic seeded CPU simulation and camera-facing raster billboard geometry; editable bounded emission/lifetime/gravity/size | Raster-only, no RT/PathTracing participation or particle shadows; no GPU simulation/collision system. Settings persist, live particles do not. |
| Extensions | Real `.dll`/`.dylib` loading, versioned C ABI, owner-thread Tick/Draw, explicit load/unload/reload, stable-file watcher and bounded state migration | Manual external CMake compilation. Trusted native code in-process, no sandbox/background callbacks/ImGui ABI. Editor extensions are not automatically packaged gameplay modules. |
| Packaging | File → Build package; staged dependency closure, fingerprints, runtime/library/shader/resource inputs, generated launcher; `--runtime` manifest startup | Windows/macOS, same OS and architecture as the input binary. No installer, signing/notarization, cross compilation or implicit source-tree deployment. Missing dependencies/tools/collisions fail publication. |
| Diagnostics | Project Console filters/commands, classic console completion/history, file JSON logs, editor profiler/import/cache counters | Bounded recent records and rotating files. Counters/UI timings are not an engine-wide FPS or frame-time guarantee. |

## Entry points and controls

`File` owns projects, scene files, recovery, Prefab instantiation and package creation. `Add` and Hierarchy's `+ Add` create built-in entities; models are added through `Project tools → Assets`. A model tile double-click or Scene drop creates an instance; an authored `.jscene` activation replaces the scene behind a dirty guard. Use File → Instantiate prefab for `.jprefab`; the classic `.scene` format belongs to the classic Scene Browser/PathTracing path.

The default camera uses WASD, right-drag look, right-mouse E/Q vertical movement, F to focus and Alt-left orbit about the focus. Shift+W/E/R select gizmo tools and never also move the camera; Q without the right mouse toggles local/world axes. Ctrl+G toggles snap. Ctrl+S/O/N, Ctrl+Z/Y/Shift+Z, Ctrl+D and Ctrl+C/V cover files/history/reuse. macOS Command is normalized to the same editor shortcut modifier in the authoring UI. Text edits, popups and gizmo interaction gate scene input. Project Settings can replace the six movement keys while retaining those focus/modifier gates.

Hierarchy Ctrl/Command-click adds selection. Duplicate/Delete and clipboard Copy/Paste operate on selected roots, so selecting a parent and its child does not process the child twice. Paste selects the newly created roots. Reparent attempts to preserve world placement and rejects cycles/locked edits or nonrepresentable local TRS. Inspector numbers are local values and rotation is degrees. Models retain individual mesh offsets, rotations and scale when their entity or parent moves.

## Persistence and data ownership

[Document](../jEngine/Code/Engine/Editor/jEditorDocument.h) contains plain CPU data, owner-thread transactions and bounded history. Default limits are 10,000 entities, 16 MiB scene files, 128 history entries/32 MiB history and hierarchy depth 256. Strict versioned parsing and finite-number/path validation run before replacing the live document. A failed document parse preserves the previous document; a valid document with missing model assets can still fail later runtime publication. Scene Save backs up the previous file as `<scene>.bak`. The UI creates a save directory; the lower-level Save API requires its parent directory to exist.

The UI's dirty guard handles New/Open/project replacement and window close. Recovery goes to `<project>/.jeditor/recovery.jscene` only when dirty, not playing and outside a transaction. Layout/ImGui state live separately under `.jeditor/`. Saving a scene inside the open project updates its startup-scene setting. Save the desired scene before building a package because packaging saves the current scene first.

[Runtime](../jEngine/Code/Engine/Editor/jEditorRuntime.cpp) maps stable document IDs to runtime bindings. External scene objects are borrowed rather than deleted from their original owner. Authored instances own independent objects/render elements and share mesh/material resources. Visibility changes real renderer/picking/light registration. Removal retires owned resources until scene references and GPU consumers are safe. Scene publication, GPU work and teardown remain on the engine owner thread; workers never mutate scene registries or RHI objects.

## Asset work and admission budgets

[Asset registry](../jEngine/FileLoader/jEditorAssets.h) records stable IDs independently of names/content/index, source fingerprints, import settings and dependencies. Catalog scans publish a completed snapshot on the owner thread; a newer request supersedes older scan work. The open-project UI checks periodically and provides Refresh, Reimport and Cancel scan. Metadata moves with its source. Trash is recoverable project-local `.jeditor/trash/`, not permanent deletion or automatic scene Undo.

Authoring model requests deduplicate by source/asset/revision. CPU preparation uses the existing bounded import service. The [upload session](../jEngine/FileLoader/jModelUploadSession.h) admits actual texture/geometry copy chunks on the owner thread, polls submission fences and only publishes a completed model. The editor currently targets **8 MiB, 2 ms CPU admission and two pending chunks per step**, with one session admission step per editor update and at most two held upload payloads. Status includes completed bytes, worst admission time and reported admission overrun. Failed replacement keeps the last good instance; cancellation stops further work and retires submitted buffers safely.

These are copy-admission targets, not upper bounds on driver resource allocation, acceleration-structure builds, shader compilation, primitive/terrain regeneration or total frame time. A resource allocation can exceed 2 ms and is reported as such. The legacy `FinalizeModel` adapter drains the same session synchronously; the classic Model Scene Browser still uses that blocking completion adapter after background CPU preparation. Legacy PathTracing scene loading remains synchronous and may discard the old scene before a later failure.

Thumbnails prepare bounded PNG pixels on workers and upload small UI textures on the owner. The asset panel caches at most 64 previews and bounds queued requests. An image change invalidates decoded/managed caches and matching authored material bindings; support for rendering a material or preview depends on the active renderer/backend, rather than an asset-kind label alone.

## Render settings, materials and procedural tools

Project Settings validates a draft, atomically saves the project, then publishes renderer defaults and editor camera bindings. Known keys are `render.deferred`, `render.surfelgi`, `render.surfelgi.inline`, `render.ssgi`, `render.bloom`, `render.vsync`, `render.surfelgi.intensity` (0..8), `render.surfelgi.inline.rays` (1..16), and `input.camera.forward/back/left/right/up/down`. Unknown project settings are retained. Conflicting camera keys are rejected; changing bindings does not remove viewport/text/modifier safety.

An Inline request requires actual device/driver support and a usable scene acceleration structure. Apple M4's verified experimental MoltenVK ray-query path differs from the stable Homebrew runtime; setting an environment variable cannot add missing driver support. Use the [platform guide](platform-and-ci.md#surfelgi-hardware-inline-ray-queries) for setup and stable fallback. Existing raster defaults do not redefine the legacy PathTracing pipeline.

The graph compiles limited PBR properties into real material overrides; it does not generate arbitrary HLSL. Imported originals remain intact unless overridden. Terrain brushes build actual heightfield geometry, and navigation uses the selected local terrain mesh via Recast/Detour. Particle simulation runs on CPU; its render object explicitly excludes ray tracing. These boundaries apply even when typed data fields are serializable.

## Native editor ABI and reload lifecycle

The [C header](../jEngine/Code/Engine/Editor/jEditorExtensions.h) defines `jengine_plugin_v1()` with ABI `0x00010000`, descriptor/host sizes, OnLoad/Tick/Draw/OnUnload and an optional paired SaveState/RestoreState. Host callbacks expose Log/CreateEntity/SetTransform and Text/Button/DragFloat3 without engine or ImGui object layouts. UI callbacks are valid only during Draw. All calls are synchronous on the owner thread; no plugin background callbacks or retained UI pointers are supported.

The loader stages an exclusive shadow copy, validates source stability and ABI, snapshots at most 64 KiB state, then loads/restores a candidate inside a document transaction. Rejected startup/restore rolls back candidate document edits and preserves the old module. Old callbacks stop before unload. A watcher checks periodically and attempts a changed file only after its timestamp/size are stable for at least 500 ms. Rebuild externally; the engine does not run a compiler. New project/scene operations shut down extensions because saved entity IDs belong to the current document.

Build the [standalone example](../examples/editor-plugin/README.md) from the repository root:

```sh
cmake -S examples/editor-plugin -B build/editor-plugin -DCMAKE_BUILD_TYPE=Debug
cmake --build build/editor-plugin --config Debug
```

Load the resulting native library through Window → Extensions / hot reload. Libraries execute trusted code in the process and need their external dynamic dependencies deployed normally. The fixture option builds real versioned/bad-ABI/load/restore/oversized/missing-export libraries for regression tests; tests exercise real OS library loading rather than only function-pointer substitutes.

## Project and portable-runtime CLI

Development startup accepts a project and optionally an explicit authored scene:

```sh
./scripts/run-macos.sh --project /absolute/path/Project/project.jproject
./scripts/run-macos.sh --project /absolute/path/Project/project.jproject --scene /absolute/path/Project/Scenes/Main.jscene
```

Windows uses its built `jEngine.exe` with the same arguments and one RHI selector. File → Build package saves the current scene and builds a new output directory containing a validated `jengine-runtime.json`, project/scene dependency closure, explicit runtime executable/libraries, shaders/resources and platform launcher. macOS packaging also prepares the DXC and selected MoltenVK ICD/library paths; Windows includes required DLLs and applicable D3D12 runtime files. Required platform tools such as CMake must be available during packaging.

Run the generated `run-jEngine.command` on macOS or `run-jEngine.cmd` on Windows after moving the **whole** output folder. The direct runtime entry is:

```sh
./runtime/jEngine --runtime /absolute/path/Package/jengine-runtime.json
```

Use `runtime/jEngine.exe` on Windows. The macOS launcher additionally establishes its packaged DXC/driver environment. `--runtime` cannot be combined with `--project`/`--scene`; it verifies fingerprints, resolves paths relative to the manifest, starts the scene in Play and hides authoring UI. There is no `--package` build command in the current CLI. Packaging is same-platform staging, not a cross compiler, installer or signing system; loaded editor extensions are not automatically included or launched as gameplay plugins.

## Validation evidence

The implementation has dedicated CPU suites for document persistence/history, runtime bindings, project/assets/settings/packaging, tools/navigation, procedural/material behavior, native shared-library reload and model upload planning, plus shader and GPU integration checks. This guide records source-level behavior; it does **not** assert that every current integration run or target GPU is green. Final exact test totals, platform results, captures and timing conditions belong in the [performance/integration report](reports/editor-foundation-performance.md). Existing classic screenshots and older measurements remain evidence only for their recorded source and workload.
