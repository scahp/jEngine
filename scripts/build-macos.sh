#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dxc="${JENGINE_DXC_EXECUTABLE:-$(dirname "$root")/DirectXShaderCompiler/build/macos/bin/dxc}"
if [[ ! -x "$dxc" ]]; then
  echo "DXC missing: $dxc. See README macOS setup, or set JENGINE_DXC_EXECUTABLE." >&2
  exit 1
fi
cmake -S "$root" -B "$root/build/macos" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJENGINE_DXC_EXECUTABLE="$dxc"
cmake --build "$root/build/macos" -j "${JENGINE_BUILD_JOBS:-6}"
