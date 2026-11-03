#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/macos"
if [[ ! -x "$out/jEngine" ]]; then echo "Build the engine first." >&2; exit 1; fi
log="$out/verification.log"
VK_LAYER_PATH="$(brew --prefix vulkan-validationlayers)/share/vulkan/explicit_layer.d" \
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
JENGINE_MAX_FRAMES=120 JENGINE_CAPTURE_FRAME="$out/frame-60.png" \
  "$root/scripts/run-macos.sh" > "$log" 2>&1
if grep -Eq 'Validation (Error|Warning):|\[mvk-error\]' "$log"; then
  echo "Vulkan validation failed; inspect $log" >&2
  exit 1
fi
grep -Eq 'jEngine clean shutdown; rendered frames: 120' "$log"
test -s "$out/frame-60.png"
echo "PASS: rendered 120 frames, saved frame-60.png, clean exit, no Vulkan validation messages."
