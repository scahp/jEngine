# jEngine

[![Windows](https://github.com/scahp/jEngine/actions/workflows/ci-windows.yml/badge.svg?branch=main)](https://github.com/scahp/jEngine/actions/workflows/ci-windows.yml)
[![macOS](https://github.com/scahp/jEngine/actions/workflows/ci-macos.yml/badge.svg?branch=main)](https://github.com/scahp/jEngine/actions/workflows/ci-macos.yml)
[![CMake](https://img.shields.io/badge/CMake-Windows%20%7C%20macOS-064F8C?logo=cmake)](CMakePresets.json)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](CMakeLists.txt)
[![Vulkan](https://img.shields.io/badge/Vulkan-supported-AC162C?logo=vulkan)](#supported-platforms)
[![DirectX 12](https://img.shields.io/badge/DirectX-12-107C10)](#supported-platforms)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A personal 3D engine and editor using Vulkan and DirectX 12.
 - Documentation : https://scahp.tistory.com/120

[**User manual (Korean)**](docs/manual/README.md) — numbered screenshots, scene loading,
projects, scene editing, assets, authoring tools, and packaging.
[Editor guide](docs/editor-authoring.md) · [Implementation and measurements](docs/reports/editor-authoring.md) · [Studio sample](examples/authoring-demo/README.md)

## Supported platforms

- **Windows**: DirectX 12 and Vulkan.
- **macOS (Apple Silicon)**: Vulkan via MoltenVK and Vulkan portability extensions.

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

## Windows

Open `jEngine/jEngine.sln` in Visual Studio 2026 and build **x64**.
DXC downloads automatically on first use and is reused afterward.

For CMake, install CMake 4.2+ and Python 3, then run:

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Open the generated solution in `build/windows-vs2026/`, or use `windows-release`
for Release builds. The Visual Studio C++ tools are required in both cases.

## macOS (Apple Silicon)

Requires Xcode Command Line Tools and Homebrew. Vulkan runs on Metal via MoltenVK.

```sh
./scripts/setup-macos.sh
./scripts/run-macos.sh
```

SurfelGI ray queries and the existing path tracer require the experimental MoltenVK runtime.
See [setup details](docs/platform-and-ci.md#surfelgi-hardware-inline-ray-queries) and [verified M4 scenes](docs/reports/macos-pathtracing.md).

## GitHub Actions

Windows and macOS Debug/Release builds, CPU tests, and shader checks run on every
code push and on PRs targeting `main`. Build artifacts and logs are kept for five days.
[Workflows](https://github.com/scahp/jEngine/actions) · [Details](docs/platform-and-ci.md#github-actions-ci)

[Engine roadmap](docs/engine-roadmap.md) · [Editor foundation measurements](docs/reports/editor-foundation-performance.md)

## Third party libraries

See [third-party notices](THIRD_PARTY_NOTICES.md).

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
14. Recast Navigation : https://github.com/recastnavigation/recastnavigation
