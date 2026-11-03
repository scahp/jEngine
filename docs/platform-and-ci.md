# Platform and CI details

## macOS (Apple Silicon / MoltenVK)

The macOS CMake target runs the existing Vulkan renderer through MoltenVK. Windows
can generate a Visual Studio solution with the `windows-vs2026` preset; the
handwritten `jEngine/jEngine.sln` remains available. Install Xcode Command Line Tools and Homebrew,
then run from the repository root:

```sh
./scripts/setup-macos.sh  # installs dependencies and builds native DXC plus jEngine
./scripts/run-macos.sh
```

For subsequent builds, use `./scripts/build-macos.sh`. DXC is pinned to upstream
`v1.9.2609` (`01b62ad47db7dee3dd33f90e7339cf9e860b3f93`) with SPIR-V enabled,
in a sibling `DirectXShaderCompiler` checkout. Override `JENGINE_DXC_SOURCE` during
setup, or `JENGINE_DXC_EXECUTABLE` during engine builds, to use another location.
The run script selects Vulkan and sets the working directory for existing assets.

The port uses native GLFW/Assimp, stb for ordinary/HDR images, and tinyddsloader
(MIT) for DDS mip chains and cubemaps. It generates missing image mips on the GPU.
It enables portability enumeration/subset features, supports integrated GPUs and
shared graphics/compute/transfer queue families, and uses supported depth formats.
Geometry shaders and NVIDIA VRS are unavailable on this Mac. SurfelGI now uses
its normal default and existing UI controls. Inline ray gathering is enabled only
when the selected Vulkan device supports acceleration structures and ray queries.
DX12 and Vulkan ray-tracing pipelines have not been validated on macOS; the macOS
RT-pipeline path remains disabled. The CMake baseline uses the legacy profiler; Tracy remains
the Windows default. The portable PPL fallback currently executes loops serially.

A bounded rendering check and frame capture can be run with:

```sh
JENGINE_MAX_FRAMES=120 \
JENGINE_CAPTURE_FRAME="$PWD/build/macos/frame-60.png" \
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
./scripts/run-macos.sh
```

The PNG is read back from the rendered swapchain at frame 60; the program exits
normally after the requested number of frames. Omit those environment variables
for interactive use. If validation layers are not discovered, set `VK_LAYER_PATH`
to `$(brew --prefix vulkan-validationlayers)/share/vulkan/explicit_layer.d`.

`./scripts/verify-macos.sh` performs this bounded check and fails on Vulkan
validation messages, driver errors, a missing capture, or abnormal termination.
Verified on Apple M4 with MoltenVK 1.4.2: Sponza PBR deferred rendering, 120 frames,
PNG capture, and normal shutdown with zero Vulkan validation errors/warnings.
Existing HLSL vector-truncation compiler warnings remain. Windows builds were not
executed on this Mac.

### SurfelGI hardware inline ray queries

A separately built experimental MoltenVK runtime enables Metal-backed Vulkan ray
queries on the M4. It is pinned to unmerged upstream PR #2771, commit
`176926b4eb7b6bd70f46a59f277b4417dd235f67`, including its pinned SPIRV-Cross fork.
It uses public Metal APIs, without GPU identity spoofing or private API options.
The CMake build was verified with the installed Command Line Tools.

After the base dependencies above are available:

```sh
./scripts/setup-macos-rayquery.sh
./scripts/verify-macos-rayquery.sh
./scripts/run-macos.sh
```

Setup builds the driver in a sibling `MoltenVK-RayQuery` directory and checks a
known triangle against 256 GPU ray queries. Verification checks SurfelGI with
inline rays on, off, and paused/resumed in one process, plus the stable-driver
fallback. It reads back actual surfel irradiance and resolve pixels, compares the
rendered on/off images, and rejects Vulkan/Metal validation errors. Only after all
checks pass does it activate this driver for the normal run script. The Homebrew
installation is preserved; no system driver or scheduler is changed.

Use the existing **SurfelGI → Enable SurfelGI** and **Enable Inline Ray Gather**
checkboxes, ray count, guiding, distance, bias and history controls. Turning inline
gather off pauses new ray samples while retaining existing irradiance history;
placement can still seed new surfels from neighboring history. Turning SurfelGI
off skips its contribution. Unsupported drivers show the inline checkbox disabled.

To select the original Homebrew driver explicitly:

```sh
JENGINE_RUNTIME=stable ./scripts/run-macos.sh
```

The verified runtime is selected per process through its ICD manifest and
`MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING=1`; that variable alone does not add RT
support to the stable MoltenVK release. The first known-good executable remains
under `build/macos-stable/`. Reports, logs and opaque PNGs are in
`build/macos-rayquery/`. Existing Windows DX12/Vulkan builds still use the solution;
Windows execution has not been tested on this Mac.

The Vulkan shader generator now emits C++ member byte offsets and trailing struct
padding explicitly. This keeps scalar-layout SPIR-V compatible with aligned CPU
uniforms and structured buffers. The previous mismatch produced valid surfel
samples but a black GI resolve output. Bindless index buffers likewise use Vulkan
storage-buffer declarations while retaining DX12 typed-buffer declarations.

## GitHub Actions CI

`Windows` and `macOS` run independently on branch pushes, pull requests targeting
`main`, and manual dispatch. README-only and `docs/`-only changes are skipped. It uses standard public-repository GitHub-hosted runners:

- Windows Server 2025 / Visual Studio 2026: CMake-generated Visual Studio projects,
  x64 Debug and Release. The separate Tracy profiler application is not part of CI.
- macOS 26 / Apple Silicon: CMake/Ninja Debug and Release.
- Both platforms run `--ci-cpu` and compile actual engine-generated SurfelGI
  candidate gathering/selection, placement, history snapshots, resolve, visualization,
  hover selection, and inline-gather declarations to SPIR-V and DXIL. The
  SPIR-V member offsets and structured-buffer strides are compared with C++
  metadata. A deliberately unaligned control shader must be rejected.

These tests return before window/device initialization. They validate builds,
CPU math, shader generation, and layout contracts; they do not claim that a
hosted runner rendered a frame or exercised M4 hardware ray tracing. Real GPU
verification remains available through `scripts/verify-macos-rayquery.sh` on the
M4 and is not automatically scheduled on this personal computer.

Local CMake presets use the same configure/build/test commands as macOS CI:

```sh
cmake --preset macos-debug -DJENGINE_DXC_EXECUTABLE=/path/to/dxc
cmake --build --preset macos-debug
ctest --preset macos-debug
# Or configure, build and test in one command after configuring DXC discovery:
cmake --workflow --preset macos-release
```

`CMakeUserPresets.json` is ignored for machine-specific overrides. Windows requires
CMake 4.2+ for the Visual Studio 2026 generator, MSVC x64, and Python 3. Configure
with `cmake --preset windows-vs2026`; build/test with `windows-debug` or
`windows-release`. The generated solution lives in `build/windows-vs2026/`.
The CMake target includes both DX12 and Vulkan, retains Tracy, uses the bundled
Windows dependencies and the selected DXC package's `dxcompiler.dll`/`dxil.dll`,
and copies runtime DLLs beside the executable, preserving
the Agility SDK's `D3D12/` subdirectory. The IDE working directory is `jEngine/`
for existing asset paths. CPU and shader tests use the same CTest interface as
macOS and do not require a GPU or a Vulkan driver on the runner.

DXC is pinned in `scripts/ci/dependencies.json`. macOS builds it once per cache
key and shares its native executable/library between the build configurations;
Windows verifies the official release ZIP's SHA-256. macOS object compilation
uses a bounded 256 MB ccache per configuration. Workflow actions are pinned to
commit SHAs and permissions are read-only. Windows superseded runs are cancelled;
macOS lets the active run finish so its first native-DXC build can populate the cache.

Each configuration uploads build outputs and diagnostics even after a failure:
compiler logs, CTest XML on macOS, MSBuild logs/binlog on Windows, generated HLSL,
SPIR-V/DXIL, and the shader-contract JSON report. Artifacts expire after five days.
The binaries are developer build artifacts, not self-contained installers: scene
assets, runtime dependency bundling, signing, and release publication are a
separate packaging step. No release or self-hosted runner is created by this CI.

The preserved `jEngine.vcxproj` also requires `scripts/ci/setup-windows-dxc.ps1`
before building; `JEngineDxcDirectory` can select an alternative matching package.
Windows CI checks CMake and the native project in both configurations. Runtime
shader smoke checks compile real generated resolve/gather source through the
application wrapper and check the loaded compiler path/version. CMake builds also
compare the deployed DLL bytes with the selected package.
The CMake runtime dependency refreshes the DLL pair even when no source relink is needed.

See [the full-system audit](system-audit.md) for repairs, regression coverage and limits.
