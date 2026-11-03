# Full-system audit

The audit is isolated on `codex/full-system-audit`, based on the earlier SurfelGI
fixes. The director integrated all six specialist roles. No main-branch merge was
performed.

## Coverage and repairs

| Role | Existing code inspected | Main repairs |
| --- | --- | --- |
| Director | CMake, native Windows project, CI, setup and integration | Removed an undeclared `rg` dependency that broke hosted macOS setup; preserved cold DXC builds; propagated custom DXC paths; deployed matching Windows compiler/validator DLLs and verified both build paths. |
| Core | Allocators, resource pools, interned names, math, platform adapters | Fixed live allocation aliasing, free-list corruption, mismatched frees and size overflow; corrected resource-map locks, byte-span/wide-name caches, quaternion homogeneous/180-degree conversion, and bounded secure-scanning compatibility. |
| Graphics | Vulkan queues, barriers, memory, acceleration structures, descriptors and fixed state | Waited for the submitted fence; corrected mixed timeline waits, shader/AS write dependencies, readback memory and in-flight TLAS uploads; stopped repeated instance appends, rebuilt changed topology, sized descriptors by their actual types/counts, and fixed viewport-vector self-initialization. |
| Editor | Placement, console, scene/mesh import, image decoding, loader workers | Fixed deletion invalidation, duplicate registration, input capture, temporary-console-variable use-after-free, numeric parsing, missing/mixed mesh attributes, invalid faces/indices, compressed mip sizes, malformed texture failures and worker completion/error handling. |
| Network | Input/file boundaries and runtime lifecycle | No first-party network protocol exists. Fixed file append/range/token/reopen ownership, preserved argv values, rejected invalid runtime options before GPU initialization, applied bounded DX12 execution and restored character input forwarding. |
| Particles | Existing postprocessing/AO/SSGI/atmosphere effects | No first-party particle simulator exists. Fixed exposure reduction indexing/races and initialization, tiny bloom extents, blur/dispatch/UV boundaries, SSGI ray origins/history, duplicate copy bindings and missing UAV transitions. |
| DevRel | Dependency/setup claims, runtime compiler and notices | Added real Windows runtime shader compilation checks, distinguished the old bundled DLL from the CI compiler, verified DXC defaults, restored the missing ImGui notice and indexed inspected local notices. |

Vendor implementation files were not modified. New helpers support existing
engine paths; networking and particle features were not invented during this audit.

## Reproductions that changed the implementation

- Before the allocator fix, two live big allocations could alias. Oversized
  requests could wrap, and cached pages were freed using the wrong allocation API.
- Deleting a placed object invalidated the container entry before a second erase.
  Destroying a temporary console variable then executing `help` produced an ASan
  heap-use-after-free. File token/range inputs reproduced a memory write fault.
- A moving-scene GPU test exposed repeated 52-instance appends. This overflowed
  the original TLAS capacity and grew bindless arrays until allocations failed.
  The repaired test runs repeated rebuild/update/removal contracts and real
  moving TLAS frames without growing the instance list.
- Enabling SSGI and separable blur exposed a texture and unused uniform both
  declared at binding zero. After removing that obsolete binding, validation
  exposed missing UAV transitions on reused compute/history targets. These were
  repaired rather than suppressing validation.
- Existing Windows tests used the pinned DXC executable, while the application
  loaded an older bundled DLL. The new runtime test reproduced `select` and
  SPIR-V-target failures with DLL 1.6.2106.3. Both CMake and the preserved native
  project now deploy the selected DXC package's compiler/validator pair.

## Verification

- Local macOS Debug and Release: CTest CPU, process-input, shader-layout and Vulkan
  method-contract suites pass. The full engine also passes the CPU suites with
  AddressSanitizer and UBSan; targeted allocator/resource-pool concurrency checks
  passed ThreadSanitizer.
- Shader checks compile 25 actual generated fixtures for both SPIR-V and DXIL:
  50 compilations, five CPU/SPIR-V layout contracts and a rejected misalignment
  negative control.
- Input checks exercise 13 separate process-level invalid/bypass cases before any
  window/GPU creation. Vulkan method tests execute production method bodies
  against a deterministic driver stub, including forced topology changes and
  descriptor allocation/cache failure handling.
- Apple M4 GPU: real 256-word readback, split submissions and moving TLAS updates
  at frames 31/61/91 pass. Effects run at 1283x723 and 17x17 for 120 frames each;
  actual R16F exposure readbacks at frames 1, 2 and 90 are positive and finite.
  Vulkan/Metal validation reports no errors. SurfelGI on/off, pause/resume,
  stable-driver fallback and runtime reconfiguration also pass 120-frame runs.
- Hosted Windows CMake/native Debug and Release both pass at `fdbd57b7`
  ([workflow run](https://github.com/scahp/jEngine/actions/runs/41686710026)).
  The runtime smoke tests compile generated resolve/gather shaders
  through the application's actual compiler wrapper, log its DLL version/path,
  and check the deployed package bytes for CMake builds. The native-project log
  confirms DXC DLL version `1.9.2609.5` and all four DXIL/SPIR-V compilations pass.
- Hosted macOS Debug and Release both pass at `fdbd57b7`, including the CPU,
  process-input, shader-layout and Vulkan method-contract suites
  ([workflow run](https://github.com/scahp/jEngine/actions/runs/41686710046)).

The independent director review found no remaining integration blocker at
`fdbd57b7`.

Reproduce from the repository root:

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
python3 scripts/verify-system-gpu.py --icd /path/to/verified/MoltenVK_icd.json
python3 scripts/verify-surfel-gi.py --icd /path/to/verified/MoltenVK_icd.json
```

```powershell
./scripts/ci/setup-windows-dxc.ps1
./scripts/ci/build-windows.ps1 -Configuration Debug
./scripts/ci/build-windows-native.ps1 -Configuration Debug
```

Reports and captures remain in ignored `build/full-audit/` directories. Windows
hardware rendering and manual UI click/drag interactions were not validated on
this Mac. Tests cover the reported cases rather than an exhaustive scene corpus,
all optional render modes, long-duration soak testing or fuzzing. The macOS CRT
adapter supports the existing narrow scanning formats and rejects unsupported
length modifiers/conversions. See [third-party notices](../THIRD_PARTY_NOTICES.md)
for the explicitly partial attribution index.
