# SurfelGI audit

Baseline: `be8f707d` on `main`. Changes are isolated on `codex/surfel-gi-audit`.

| Finding | Trigger and effect | Change |
| --- | --- | --- |
| P1: multi-bounce history race | Gather samples another surfel from the same irradiance UAV being written by concurrent threads. Results can depend on GPU scheduling. | Snapshot history after placement; all gather history reads use a separate SRV. |
| P1: reservoir winner race | An atomic maximum is followed by a normal score store inside a spin lock. Another invocation can raise the score between the comparison and store, then lose that maximum or fail to publish its index. | Two dispatches: atomic maximum score, then atomic minimum candidate index among matching scores. |
| P1: unsupported allocation/dispatch sizes | Large clipmap dimensions exceed the physical pool; a full-resolution candidate grid can exceed the 65,535-group linear dispatch limit. | Reject unsupported counts before allocating and skip resolve/apply for that frame. |
| P2: stale layout/history | Changing page stride without changing the pool capacity reinterprets existing slots. Changing world cell size is absent from the clipmap reset condition. | Recreate primary storage on stride changes; clear a cascade on cell-size changes. |
| P2: lookup bounds | Resolve, visualization, and hover selection use requested slots even if the physical page was reduced by the budget. | Clamp to physical page size and check the final pool index. |
| P2: hover coordinates | Windows Vulkan casts a GLFW window to HWND; macOS always rejects the cursor. | GLFW cursor lookup with framebuffer scaling on Vulkan; keep HWND handling for DX12. |
| P2: zero-distance resolve | A pixel exactly at a surfel center normalizes a zero vector. This can produce invalid weights depending on shader/compiler behavior. | Guard the reciprocal length. |

The earlier Vulkan CPU/SPIR-V byte-offset fix is present and remains covered by
reflection checks and a deliberately misaligned negative control.

## Validation

Local Apple M4, pinned experimental MoltenVK runtime:

- Inline rays on/off, pause/resume, stable-driver fallback: 120 frames each,
  normal shutdown, no Vulkan/Metal validation messages.
- GPU winner scores/indices are compared against a CPU reduction of the actual
  candidate buffer. Every observed mismatch count is zero.
- Active history counts advance exactly once from the snapshot; observed failures
  are zero. Resolve readback rejects R11G11B10 NaN/infinity; none were found.
- Inline-on produced 1,577 lit surfels and 905,002 nonzero resolve pixels in the
  initial regression run. Inline-off produced zero sampled surfels/energy.
- Runtime reconfiguration changes cell size at frame 30 and page size 2 to 5 at
  frame 60, with constant pool capacity. All cascades reset at frames 31 and 61.
  Setting 512-cubed dimensions at frame 90 is rejected before allocation and the
  application completes normally.
- Headless CTest compiles eight real shader fixtures for SPIR-V and DXIL (16
  compilations), checks CPU layout contracts and budget boundaries.

Reproduce GPU checks from the repository root with a verified experimental ICD:

```sh
python3 scripts/verify-surfel-gi.py --engine build/macos-debug/jEngine --icd /path/to/MoltenVK_icd.json
```

Reports and logs are under `build/surfel-audit/`. The test does not activate a
runtime. Headless CI does not exercise hardware ray tracing. Windows GPU execution
and manual hover interactions require separate validation; compiler checks alone
are not evidence that those paths rendered correctly.

The snapshot adds one 48-byte irradiance record per pool slot and one compute
copy dispatch per gather frame (about 55 MiB for the default pool). The old winner
lock buffer and bounded spin loop are removed. Scene coverage and stochastic
rendering are not an exhaustive correctness proof.
