#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/.ci-tools/dxc"
source_dir="${JENGINE_CI_DXC_SOURCE:-$root/.ci-tools/dxc-source}"
revision="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["dxc"]["commit"])' "$root/scripts/ci/dependencies.json")"
if [[ -x "$out/bin/dxc" && -f "$out/revision" && "$(cat "$out/revision")" == "$revision" ]]; then
  "$out/bin/dxc" --version
  exit 0
fi
if [[ ! -d "$source_dir" ]]; then
  git clone --depth 1 --branch v1.9.2609 https://github.com/microsoft/DirectXShaderCompiler.git "$source_dir"
fi
if [[ "$(git -C "$source_dir" rev-parse HEAD)" != "$revision" ]]; then
  echo "DXC source differs from the pinned revision: $source_dir" >&2; exit 1
fi
git -C "$source_dir" submodule update --init --depth 1 external/SPIRV-Headers external/SPIRV-Tools external/DirectX-Headers
cmake -S "$source_dir" -B "$source_dir/build/macos" -G Ninja \
  -C "$source_dir/cmake/caches/PredefinedParams.cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release -DHLSL_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF \
  -DSPIRV_BUILD_TESTS=OFF -DSPIRV_SKIP_TESTS=ON
cmake --build "$source_dir/build/macos" --target dxc -j "${JENGINE_BUILD_JOBS:-2}"
mkdir -p "$out/bin" "$out/lib"
cp -L "$source_dir/build/macos/bin/dxc" "$out/bin/dxc"
cp "$source_dir/build/macos/lib/libdxcompiler.dylib" "$out/lib/"
cp "$source_dir/LICENSE.TXT" "$out/"
if ! otool -l "$out/bin/dxc" | rg -q 'path @executable_path/../lib '; then
  install_name_tool -add_rpath '@executable_path/../lib' "$out/bin/dxc"
  codesign --force --sign - "$out/bin/dxc"
fi
printf '%s\n' "$revision" > "$out/revision"
"$out/bin/dxc" --version
