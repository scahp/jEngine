#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$(uname -s)" != Darwin ]]; then echo "macOS required" >&2; exit 1; fi
HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1 brew install \
  cmake ninja molten-vk vulkan-headers vulkan-loader vulkan-tools vulkan-validationlayers glfw assimp
# Upstream DXC releases do not include a native macOS binary. Build the pinned release.
dxc_source="${JENGINE_DXC_SOURCE:-$(dirname "$root")/DirectXShaderCompiler}"
if [[ ! -d "$dxc_source" ]]; then
  git clone --depth 1 --branch v1.9.2609 https://github.com/microsoft/DirectXShaderCompiler.git "$dxc_source"
fi
if [[ "$(git -C "$dxc_source" rev-parse HEAD)" != 01b62ad47db7dee3dd33f90e7339cf9e860b3f93 ]]; then
  echo "Existing DXC checkout differs from the verified release. Preserve it and set JENGINE_DXC_SOURCE to a new directory." >&2
  exit 1
fi
git -C "$dxc_source" submodule update --init --depth 1 external/SPIRV-Headers external/SPIRV-Tools external/DirectX-Headers
cmake -S "$dxc_source" -B "$dxc_source/build/macos" -G Ninja \
  -C "$dxc_source/cmake/caches/PredefinedParams.cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release -DHLSL_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF \
  -DSPIRV_BUILD_TESTS=OFF -DSPIRV_SKIP_TESTS=ON
cmake --build "$dxc_source/build/macos" --target dxc -j "${JENGINE_BUILD_JOBS:-6}"
JENGINE_DXC_EXECUTABLE="$dxc_source/build/macos/bin/dxc" "$root/scripts/build-macos.sh"
