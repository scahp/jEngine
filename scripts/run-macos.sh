#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "${JENGINE_RUNTIME:-}" == stable ]]; then
  export VK_DRIVER_FILES="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json"
  unset MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING
elif [[ -z "${VK_DRIVER_FILES:-}" && -z "${VK_ICD_FILENAMES:-}" ]]; then
  icd="$(python3 - "$root/build/macos-rayquery/verified-runtime.json" <<'PY'
import json,sys,hashlib
from pathlib import Path
try:
    report=json.loads(Path(sys.argv[1]).read_text())
    icd=Path(report['icd']);driver=Path(json.loads(icd.read_text())['ICD']['library_path'])
    if hashlib.sha256(driver.read_bytes()).hexdigest()==report['driver_sha256']: print(icd)
except (OSError,ValueError,KeyError): pass
PY
)"
  if [[ -n "$icd" ]]; then
    export VK_DRIVER_FILES="$icd"
    export MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING=1
    export MVK_CONFIG_LOG_LEVEL="${MVK_CONFIG_LOG_LEVEL:-1}"
  fi
fi
cd "$root/jEngine"
exec "$root/build/macos/jEngine" -vulkan "$@"
