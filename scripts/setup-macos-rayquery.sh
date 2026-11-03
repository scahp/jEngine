#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="${JENGINE_MVK_SOURCE:-$(dirname "$root")/MoltenVK-RayQuery}"
revision=176926b4eb7b6bd70f46a59f277b4417dd235f67
if [[ ! -d "$source_dir" ]]; then
  git clone --depth 1 --branch macgaming/ray-query-pr https://github.com/dttdrv/MoltenVK.git "$source_dir"
  git -C "$source_dir" checkout --detach "$revision"
fi
if [[ "$(git -C "$source_dir" rev-parse HEAD)" != "$revision" ]]; then
  echo "Existing experimental driver differs from the verified revision; choose a new JENGINE_MVK_SOURCE directory." >&2
  exit 1
fi
cmake -S "$source_dir" -B "$source_dir/build/macos" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMVK_EXCLUDE_SPIRV_TOOLS=ON -DMVK_EXCLUDE_CEREAL=OFF -DMVK_USE_METAL_PRIVATE_API=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$source_dir/build/macos" -j "${JENGINE_BUILD_JOBS:-6}"
out="$root/build/macos-rayquery"
mkdir -p "$out"
python3 - "$source_dir" "$out" <<'PY'
import json,sys
from pathlib import Path
source,out=map(Path,sys.argv[1:])
d={'file_format_version':'1.0.0','ICD':{'library_path':str(source/'build/macos/MoltenVK/libMoltenVK.dylib'),'api_version':'1.4.0','is_portability_driver':True}}
(out/'MoltenVK_icd.json').write_text(json.dumps(d,indent=2)+'\n')
PY
brew_root="$(brew --prefix)"
glslangValidator -V --target-env vulkan1.2 "$root/platform/macos/tests/ray-query.comp" -o "$out/ray-query.spv"
clang++ -std=c++17 -I"$brew_root/include" "$root/platform/macos/tests/ray-query-probe.cpp" \
  -L"$brew_root/lib" -lvulkan -o "$out/ray-query-probe"
VK_DRIVER_FILES="$out/MoltenVK_icd.json" MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING=1 \
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation MTL_DEBUG_LAYER=1 MVK_CONFIG_LOG_LEVEL=2 \
  "$out/ray-query-probe" "$out/ray-query.spv" > "$out/ray-query-probe.log" 2>&1
if ! rg -q 'rays=256 hits=32 mismatches=0' "$out/ray-query-probe.log" || \
   rg -q 'Validation (Error|Warning):|\[mvk-error\]|failed assertion' "$out/ray-query-probe.log"; then
  echo "Ray-query probe failed; inspect $out/ray-query-probe.log" >&2
  exit 1
fi
cmake -S "$root" -B "$out" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$out" -j "${JENGINE_BUILD_JOBS:-6}"
echo "Driver and ray-query probe passed. Run scripts/verify-macos-rayquery.sh to validate the engine and activate this runtime."
