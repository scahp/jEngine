# Advanced shadow examples

Open project.jproject. Its startup scene is Scenes/deferred-csm4.jscene (four stabilized cascades, Deferred). The harness ends its captures on deferred-legacy-restored.jscene; that final control is not the project startup scene.

Each scene stores renderable shadowCaster flags and explicit settings on every active shadow-casting light. Those settings do not depend on the global console defaults used by the automated comparisons. Use the saved primary camera (Play) to reproduce the authored viewing direction. Choose Forward in the viewport renderer menu for scenes whose names start with forward-; otherwise use Deferred.

| Feature | Scene files |
| --- | --- |
| CSM | deferred-csm4, deferred-csm4-camera-moved, forward-csm4 |
| VSM / ESM / EVSM | deferred-vsm-*, deferred-esm-*, deferred-evsm-*, forward-evsm |
| Deep opacity layers | deferred-deep-half, deferred-deep-three-quarter, forward-deep |
| Dual Paraboloid | deferred-dual-front-back-seam, forward-dual |
| Perspective shadow map | deferred-psm |
| Closed shadow volumes | deferred-volume, deferred-volume-camera-inside, forward-volume |
| Intentional rejection controls | deferred-volume-nonmanifold-fallback, per-light-override-isolation |

All names above are Scenes/<name>.jscene. The normal receiver does not cast shadows; the intentional rejection controls enable its open-mesh caster flag to exercise a diagnosed fallback. The mixed override control keeps a separate point light's VSM active. Deep scenes contain exactly two flat layers at authored opacity 0.5 or 0.75; opaque cubes remain visible but do not cast.

PSM may explicitly report a fitted-projection fallback for an unsafe configuration. The saved examples target the two raster renderers. [Native measurements, diagnostics and raw scene captures](../../docs/reports/advanced-shadows.md) document the verified run.
