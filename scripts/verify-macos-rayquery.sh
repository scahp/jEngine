#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/macos-rayquery"
cmake --build "$out" -j "${JENGINE_BUILD_JOBS:-6}" > "$out/verification-build.log" 2>&1
cd "$root/jEngine"
for mode in on off toggle stable; do
  inline=1
  toggle=0
  if [[ "$mode" == off ]]; then inline=0; fi
  if [[ "$mode" == toggle ]]; then toggle=1; fi
  icd="$out/MoltenVK_icd.json"
  if [[ "$mode" == stable ]]; then icd="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json"; fi
  env VK_DRIVER_FILES="$icd" MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING=1 MVK_CONFIG_LOG_LEVEL=2 \
    VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation MTL_DEBUG_LAYER=1 \
    JENGINE_SURFEL_GI=1 JENGINE_INLINE_RAY="$inline" JENGINE_TEST_INLINE_TOGGLE="$toggle" \
    JENGINE_SURFEL_DIAGNOSTICS=1 JENGINE_FIXED_TIMESTEP=0.016666667 JENGINE_MAX_FRAMES=120 \
    JENGINE_CAPTURE_FRAME="$out/surfel-$mode.png" \
    "$out/jEngine" -vulkan > "$out/surfel-$mode.log" 2>&1
  if grep -Eq 'Validation (Error|Warning):|\[mvk-error\]|failed assertion|error:' "$out/surfel-$mode.log"; then
    echo "Validation failed ($mode): $out/surfel-$mode.log" >&2; exit 1
  fi
  grep -Eq 'jEngine clean shutdown; rendered frames: 120' "$out/surfel-$mode.log"
  echo "Rendering/validation passed: $mode"
done
python3 - "$root" <<'PY'
from pathlib import Path
import re,json,sys,hashlib,datetime,shutil,subprocess
root=Path(sys.argv[1]);out=root/'build/macos-rayquery'
pattern=r'SurfelGI diagnostics: placed=(\d+) sampled=(\d+) lit=(\d+) invalid=(\d+) energy=([\d.e+-]+) history=([\d.e+-]+)'
results={}
dispatches={}
for mode in ('on','off','toggle','stable'):
    text=(out/f'surfel-{mode}.log').read_text()
    records=[dict(zip(('placed','sampled','lit','invalid','energy','history'),map(float,m))) for m in re.findall(pattern,text)]
    assert records, (mode,'missing diagnostic readback')
    assert all(r['placed']>0 and r['invalid']==0 for r in records), (mode,records)
    results[mode]=records
    dispatches[mode]=[tuple(map(float,m)) for m in re.findall(r'Inline gather dispatches: (\d+) anchor_history=([\d.e+-]+)',text)]
assert results['on'][-1]['lit']>0 and results['on'][-1]['history']>0
resolve_counts=re.findall(r'Surfel resolve nonzero pixels: (\d+)',(out/'surfel-on.log').read_text())
assert resolve_counts and int(resolve_counts[-1])>0, 'GI resolve output is black'
comparison=subprocess.check_output([sys.executable,str(root/'platform/macos/tests/compare-captures.py'),str(out/'surfel-on.png'),str(out/'surfel-off.png')],text=True)
(out/'image-comparison.json').write_text(comparison)
image_comparison=json.loads(comparison)
for mode in ('on','off','toggle','stable'):
    alpha=subprocess.check_output(['sips','-g','hasAlpha',str(out/f'surfel-{mode}.png')],text=True)
    assert 'hasAlpha: no' in alpha
assert results['off'][-1]['sampled']==0 and results['off'][-1]['energy']==0
assert results['stable'][-1]['sampled']==0 and results['stable'][-1]['energy']==0
before,disabled,resumed=results['toggle']
# Placement may seed new surfels from neighbors while tracing is paused. Check
# actual gather dispatches and an existing surfel's history, not the global sum.
assert dispatches['on'][-1][0]>0 and dispatches['off'][-1][0]==0 and dispatches['stable'][-1][0]==0
before_dispatch,paused_dispatch,resumed_dispatch=dispatches['toggle']
assert before_dispatch==paused_dispatch, dispatches['toggle']
assert resumed_dispatch[0]>paused_dispatch[0] and resumed_dispatch[1]>paused_dispatch[1]
assert resumed['history']>disabled['history'] and resumed['lit']>0
icd=json.loads((out/'MoltenVK_icd.json').read_text());driver=Path(icd['ICD']['library_path'])
report={'verified_at':datetime.datetime.now().astimezone().isoformat(),'driver_revision':'176926b4eb7b6bd70f46a59f277b4417dd235f67','driver_sha256':hashlib.sha256(driver.read_bytes()).hexdigest(),'icd':str(out/'MoltenVK_icd.json'),'device':'Apple M4','vulkan_validation_messages':0,'metal_validation_errors':0,'resolve_nonzero_pixels':int(resolve_counts[-1]),'image_comparison':image_comparison,'gather_dispatches_and_anchor_history':dispatches,'results':results}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
# Activate only after all modes passed. Preserve the last known-good executable.
base=root/'build/macos';stable=root/'build/macos-stable';stable.mkdir(exist_ok=True)
if not (stable/'jEngine').exists() and (base/'jEngine').exists(): shutil.copy2(base/'jEngine',stable/'jEngine')
base.mkdir(exist_ok=True);temp=base/'jEngine.next';shutil.copy2(out/'jEngine',temp);temp.replace(base/'jEngine')
(out/'verified-runtime.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: inline on/off, pause/resume, stable-driver fallback. Experimental runtime activated.')
PY
