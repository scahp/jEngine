# Scene feature sample

Open **File → Open project**, then select this folder's `project.jproject`. The project uses generated textures, procedural meshes and a precomputed SH environment; no downloaded scene models are required.

The startup scene shows two cubes on a floor with SH lighting. Open **Window → Environment / reflections / atmosphere** to enable PPR or atmosphere and save/apply the settings. PPR requires Deferred; leave its plane at `0,1,0,0` for this floor.

For material nodes, open a scene from `Scenes/parallax.jscene`, `pom.jscene`, `relief.jscene`, `interior.jscene`, `interior-dual.jscene` or `horizon.jscene`. Select the floor and open **Material graph**. The corresponding `Assets/*-on.jmaterial` and `*-off.jmaterial` files change parameters while sharing generated shader code. All texture paths are project-relative. The small colored textures make UV changes easy to see; these are diagnostic materials.

For water, return to `Scenes/scene-features.jscene`. Hide the floor, enable **Animated heightfield water** in Hierarchy, then press **Play**. Edit waves in Inspector after **Stop**. **Occluded pick control** is deliberately behind the surface and verifies that picking chooses visible water instead of the last object drawn.

The water is an opaque analytic surface. It does not simulate fluid flow, foam, transparency or refraction. PPR remains a single-plane reflection, so it does not follow individual wave crests. Mapping nodes and animated water currently require Forward or Deferred; they do not alter path-tracing geometry or collision.

[Scene feature guide](../../docs/manual/scene-features.md) · [Material node guide](../../docs/manual/material-mapping.md) · [Validation and measurements](../../docs/reports/selected-scene-features.md)
