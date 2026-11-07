# Shadow Lab calibration scene

Three opaque boxes, a receiving floor, one directional light and a saved camera. No downloaded models or textures are required.

Open `project.jproject`, then **Window → Rendering Lab**. Load the absolute path to `shadow-comparison.jlab` and press **Run experiment**. The example uses short 12-frame warmup and 24-frame measurement intervals for a quick demonstration. Increase both and use multiple repetitions for performance comparisons.

From the repository's `jEngine/` working directory:

```sh
../build/macos-debug/jEngine -vulkan \
  --project ../examples/rendering-lab/project.jproject \
  --render-lab ../examples/rendering-lab/shadow-comparison.jlab ../build/lab-runs
```

On Windows, pass the same arguments to the built executable and select `-dx12` or `-vulkan`. Use your existing Vulkan ICD setup on macOS; the command does not install or select MoltenVK.

The four variants use existing production PCF/VSM/ESM/EVSM implementations. Resolution is **per cascade tile**; the existing CSM atlas is 2×2 even with one active cascade. Moment methods additionally fit receiver/caster bounds. Differences therefore compare complete production methods, not isolated filters with identical projection matrices.

The baseline is PCF, **not a physically correct reference**. Its visible artifacts remain in the comparison. A small error against it does not prove better quality.

See the [Rendering Lab manual](../../docs/manual/rendering-lab.md) for outputs, controls and limitations.
