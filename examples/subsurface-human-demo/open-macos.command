#!/bin/bash
set -euo pipefail
sample_dir="$(cd "$(dirname "$0")" && pwd)"
engine_root="$(cd "$sample_dir/../.." && pwd)"
engine_binary="$engine_root/build/macos-debug/jEngine"
if [[ ! -x "$engine_binary" ]]; then
  echo "Build the Debug jEngine target before opening this sample."
  echo "cmake --build build/macos-debug --target jEngine --parallel 4"
  exit 1
fi
if [[ -z "${VK_DRIVER_FILES:-}" && -z "${VK_ICD_FILENAMES:-}" && -f /opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json ]]; then
  export VK_DRIVER_FILES=/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json
fi
cd "$engine_root/jEngine"
exec "$engine_binary" -vulkan --project "$sample_dir/project.jproject"
