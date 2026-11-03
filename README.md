# jEngine

Simple toy engine for implementing graphics feature with multiple api
 - Documentation : https://scahp.tistory.com/120

## Commandline argument

 - Select VulkanAPI : "-vulkan"
 - Select DX12 : "-dx12"

![image](https://github.com/scahp/jEngine/assets/6734453/bfa2b5e8-cc57-4071-883d-057477394c65)


## Feature branches

1. VRS : https://github.com/scahp/jEngine/tree/VariableShadingRate \
<img src="https://user-images.githubusercontent.com/6734453/192326454-b2be9f56-4b50-48fb-b837-1e16a5041d0c.png" width="50%"></img>
<img src="https://user-images.githubusercontent.com/6734453/192326593-1a5900b4-24d0-4b4c-991f-e58499ee8619.png" width="50%"></img> 

2. WaveIntrincs : https://github.com/scahp/jEngine/tree/WaveIntrinsics \
<img src="https://user-images.githubusercontent.com/6734453/192326936-0780c3d8-8492-409a-b4e0-34bd11125eb9.png" width="90%"></img> 

3. PBR IBL : https://github.com/scahp/jEngine/tree/PBR_IBL \
<img src="https://github.com/scahp/jEngine/assets/6734453/e080e1f0-38be-4d4b-8690-3ef17dc42a31" width="90%"></img> 

4. RaytracingOneWeekend with DXR, VkRaytracing : https://github.com/scahp/jEngine/tree/RaytracingOneWeekend \
<img src="https://github.com/scahp/jEngine/assets/6734453/393de8fe-7c50-4c33-b56a-14c572c49212" width="90%"></img> 

5. RTAO(Raytraced Ambient Occlusion) : https://github.com/scahp/jEngine/tree/RTAO \
<img src="https://github.com/scahp/jEngine/assets/6734453/bac6820a-782e-4925-9e23-9f7e3ed1e37b" width="90%"></img>

6. AsyncCompute : https://github.com/scahp/jEngine/tree/AsyncCompute \
<img src="https://github.com/scahp/jEngine/assets/6734453/f52d103b-cc2d-4673-9baf-354606ec9a36" width="90%"></img>

7. PathTracing Basis : https://github.com/scahp/jEngine/tree/PathTracingBasis \
<img src="https://github.com/scahp/jEngine/assets/6734453/0490c447-f8fe-475d-8ffc-d6b92ddac619" width="90%"></img>

8. SSGI : https://github.com/scahp/jEngine/tree/SSGI \
<img src="https://github.com/user-attachments/assets/2d338852-4f73-4d53-a524-a81860a562bd" width="90%"></img>

9. SurfelGI : https://github.com/scahp/jEngine/tree/Surfel-GI 
<img src="https://github.com/user-attachments/assets/d8f4a0ca-7bf8-4e32-b679-88ac64e07059" width="90%"/>
<img src="https://github.com/user-attachments/assets/b09eb9d6-a610-44e0-b0c5-c4a276a4802a" width="45%" />

<br/><br/>

## Third party libraries
1. ImGUI : https://github.com/ocornut/imgui
2. assimp : https://github.com/assimp/assimp
3. cityhash : https://github.com/google/cityhash
4. DirectXTex : https://github.com/microsoft/DirectXTex
5. GLFW : https://www.glfw.org/
6. loadpng : https://github.com/lvandeve/lodepng
7. robin-hood-hashing : https://github.com/martinus/robin-hood-hashing
8. glslang : https://github.com/KhronosGroup/glslang
9. ShaderConductor : https://github.com/microsoft/ShaderConductor
10. DXC : https://github.com/microsoft/DirectXShaderCompiler
11. stb : https://github.com/nothings/stb
12. WinPixEventRuntime : https://devblogs.microsoft.com/pix/winpixeventruntime/
13. xxHash : https://github.com/Cyan4973/xxHash


## macOS (Apple Silicon / MoltenVK)

The macOS CMake target runs the existing Vulkan renderer through MoltenVK. Windows
continues to use `jEngine/jEngine.sln`. Install Xcode Command Line Tools and Homebrew,
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
