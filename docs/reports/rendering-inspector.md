# Render Pass Inspector validation

The first diagnostics bundle adds a render-pass viewer, shared viewport debug modes and a pixel inspector. Pass metadata and delayed GPU query results retain their original frame IDs; a captured frame owns its preview images independently of the live ring.

## Implemented behavior

- Reuses existing GPU timestamp queries and records CPU command-encoding duration, scope hierarchy, queue type and self RHI draw/compute call counts.
- Collects named output observations without retaining pointers to transient source textures in the UI. Selected live previews and captured outputs are converted before later passes can overwrite them.
- Supports 14 viewport/output choices, marks unavailable outputs explicitly, and provides fixed-frame capture and case-insensitive pass filtering.
- Reads raw pixel values through a 16×1 RGBA32F output and 256-byte readback, retiring transfers after the corresponding swap-image wait. Existing object picking supplies object/entity/submesh identity.
- Limits preview allocation to 128 MiB. Frozen preview references prevent reuse. Unused preview textures wait through a subsequent submission of their last swap image before release, covering detached ImGui draws queued after the original main-window fence.
- Adds the new sources to the native Visual Studio project and the CMake build. Existing shader CI now covers both inspector shaders and their CPU/SPIR-V layout contracts.

The pass list is a recording-order view. It is not a calibrated multi-queue GPU timeline or a scheduling render graph. CPU times exclude work outside the recorded scopes. Preview generation itself can add GPU work; its scopes are available through **Show inspector work**.

## Verification

| Check | Result |
| --- | --- |
| macOS Debug engine build | Passed |
| `render-trace` CPU regression | Passed: delayed query/frame association, immutable CPU snapshots, parent scope identity, repeated names, resource replacement, eviction and reset |
| New shaders | 4 compilations passed: preview/pixel × Vulkan/DX12 |
| CPU/SPIR-V layout | 2 contracts passed: display/probe uniforms |
| Deferred pixel case | 120 frames, clean shutdown, known Coral box selected, finite raw values, roughness/albedo checked with storage-format precision tolerance |
| Frozen-frame case | 120 frames, frame 13 retained; after changing the live Coral box to blue at frame 40, the frame-60 screenshot still shows the original captured color and the frame-81 live pixel confirms the new blue material |
| Forward case | 120 frames, clean shutdown; final/HDR pixels present and Deferred-only GBuffer values unavailable |
| Sponza / Surfel case | 120 frames, clean shutdown; real Surfel resolve output available and finite in the pixel probe |
| Vulkan / Metal validation | No validation errors or warnings in the successful four-case run |

Actual GPU checks used Apple M4, the existing experimental MoltenVK runtime, a Debug executable linked with a disposable capture driver, and a copied `examples/authoring-demo` project. The Surfel case used the existing `sponza_pbr/sponza.glb` scene with Surfel GI/inline gather enabled and AO disabled. The engine source `Main.cpp` and original project were preserved. Captures were made at rendered frame 60, with fixed simulation time step 1/60 s and 1280×900 output. This is correctness evidence, not a performance comparison or Windows GPU validation.

The pixel check identified the Coral box at frame 9. Its GPU roughness was `0.419921875`, corresponding to the fixture's `0.42`. The report records raw GBuffer values rather than claiming bit-identical equality with authoring values in lower-precision formats. See [machine-readable evidence](rendering-inspector.json).

## Runtime issue found during verification

The legacy RTAO material table dereferenced every static render object's index buffer. The authored sample contains non-indexed geometry, causing a null dereference before the first frame completed. RTAO now rejects that unsupported table or a missing TLAS, the caller avoids consuming a null AO texture, and the inspector reports why AO was not produced. This guard does not add non-indexed geometry support to the legacy RTAO shader.

## Focused checks

From the repository root:

```sh
cmake --build build/macos-debug --target jEngine jEngineRenderTraceTests -j 6
ctest --test-dir build/macos-debug -R '^render-trace$' --output-on-failure
python3 scripts/ci/check-shaders.py \
  --engine build/macos-debug/jEngine \
  --dxc .ci-tools/dxc/bin/dxc \
  --source jEngine \
  --output build/render-inspector-shaders \
  --group render-inspector
```

The existing full shader CI includes these fixtures automatically. Windows runtime inspection remains a separate on-device check; compiling DXIL does not validate the DX12 capture lifetime on a Windows GPU.

See the [user manual and actual screenshots](../manual/rendering-inspector.md).
