# Advanced raster shadows

[Manual index](README.md) · [Scene features](scene-features.md) · [Material mapping](material-mapping.md)

Projection and shadow representation are separate settings. Directional lights can use Single, Cascaded (CSM) or Perspective (PSM) projection. Point lights can use Cube or Dual Paraboloid. The representation chooses Depth, Variance (VSM), Exponential (ESM), Exponential Variance (EVSM), Deep or Shadow Volume.

The defaults remain **Single / Cube / Depth** and keep the existing shadow path. These controls configure raster shadows in both Scene and Game; Path Tracing and HWRT ray visibility use their own shadow evaluation.

## Choose project defaults

1. Open **Window → Project settings → Renderer defaults → Shadows**.
2. Choose **Representation**, then the appropriate **Directional projection** or **Point projection**. Those two projections configure different light types. Spot lights keep their single perspective projection.
3. Adjust the controls shown for that representation. Start with the default resolution and small blur radius.
4. Click **Save and Apply**. The project manifest is saved before the validated settings are published. Invalid values leave the previous file and runtime settings intact.
5. Open **Last-frame shadow diagnostics** to inspect actual advanced-target memory, active/inactive state, pass counts and reported fallback reasons. It describes the last rendered frame, so it can differ from an unsaved project draft.

**Revert edits** reloads saved settings. **Use engine defaults** resets known renderer and camera settings in the draft; click **Save and Apply** to persist that reset. Unknown project settings remain intact.

The **Shadows** section in the F10 classic render controls uses the same widgets and validators. It changes session settings directly; use Project settings to persist project defaults. Neither UI records GPU work while drawing controls.

## Override one light

Select a directional, point or spot light, then open **Inspector → Light**. Keep **Cast shadows** enabled and check **Override shadow settings**.

An override replaces the **whole advanced settings set** for that light. It starts from the current global settings when those are compatible with the light; otherwise the UI explains that it starts from engine defaults. Only the relevant projection selector is shown. Edits participate in scene Undo/Redo and are saved with the scene and prefabs. Uncheck the override to inherit project/global defaults again.

Project changes do not replace a light's explicit override. The existing **Filter: Existing / Poisson PCF** and **Radius in texels** remain separate global depth-comparison controls; the per-light advanced override does not introduce a separate PCF setting.

## Keep the floor as a receiver

Object casting is separate from a light's **Cast shadows** switch. Select a renderable object and open **Inspector → Shadow casting**. Enable **Override shadow casting**, then turn **Cast shadows** on or off. Clearing the override restores the source/prototype's casting flag. A receiver-only object stays visible and continues to receive the scene's shadows; this control does not add a separate receive-shadows option.

For a small Deep or Shadow Volume test:

1. Select the floor in Hierarchy. In **Shadow casting**, enable its override and turn **Cast shadows** off.
2. Select a closed, opaque cube. Enable its casting override and keep **Cast shadows** on. For a Deep transparency test, use an additional object with the intended material opacity.
3. Select the light and configure its representation under **Light**. Keep that light's own **Cast shadows** on.
4. Save the scene. The floor's explicit `false` and the cube's explicit `true` survive save/load, prefabs and Undo/Redo.

The floor can now show the cube's shadow without its open plane entering the closed-volume caster topology check. Cube, Sphere, Model, ParticleEmitter, Terrain and Water entities accept this object property; their geometry must still satisfy the selected shadow method's requirements. Light entities use only **Light → Cast shadows**.

## Supported combinations

| Representation | Directional light | Point light | Spot light |
| --- | --- | --- | --- |
| Depth, VSM, ESM, EVSM, Deep | Single, CSM or PSM | Cube or Dual Paraboloid | Single perspective |
| Shadow Volume | Single only | Cube only | Point-style volume with spot-cone lighting |

CSM, PSM and Dual Paraboloid cannot be combined with Shadow Volume for the corresponding light type. The per-light editor rejects that combination. Independent global defaults may request an incompatible combination for one light type; the panel explains the conflict and the renderer reports its fallback. A projection for another light type does not constrain a spot light.

Device limits, resource budgets, invalid geometry or an unstable projective fit can still prevent an advanced result. A saved request is not proof that the advanced path was active. Read the frame diagnostic; the existing shadow result is retained as the fallback.

## Projection controls

For a first CSM setup, choose **Depth**, **Cascaded (CSM)**, four cascades and split lambda `0.5`. Set **Maximum shadow distance** to the portion of the scene that needs detailed directional shadows. **Stabilize cascades** reduces movement of the fitted projection relative to shadow texels. **Caster padding** expands the fitted caster region.

PSM changes directional-light sampling using a viewer-dependent projective fit. It is a raster projection rather than a ray-traced shadow method. The renderer uses depth clamp and checks caster positions in homogeneous clip space before accepting the fit. Degenerate or unsafe fits use a fitted orthographic fallback and report that reason.

Dual Paraboloid projects point-light shadows into two hemispheres. Its geometry preparation uses bounded subdivision; a model that exceeds that budget reports a failure instead of silently accepting an inadequate subdivision. **Geometry triangle budget** is shown for both Dual Paraboloid and Shadow Volume, with the same source-triangle cap. The current CPU preparation requires static base geometry; analytic Water uses the existing cube/depth fallback.

| Control | Default | Accepted values |
| --- | --- | --- |
| Map tile resolution | 512 | Power of two: 64, 128, 256, 512 or 1024 |
| Cascade count | 4 | Integer 1–4 |
| Split lambda | 0.5 | Finite 0–1 |
| Maximum shadow distance | 20,000 | Finite 0.01–1,000,000 world units |
| Stabilize cascades | On | Boolean |
| Caster padding | 1,000 | Finite 0–100,000 world units |
| Geometry triangle budget | 20,000 | Integer 1–65,536; shown for Dual Paraboloid / Shadow Volume |

**Map tile resolution** sizes the new advanced projection/representation targets. The unchanged Single/Cube/Depth fallback keeps its existing map sizes. A legal maximum distance must also produce a valid projection relative to the current view's near/far range.

## Moment representations

VSM filters depth moments; ESM uses an exponential depth encoding; EVSM uses positive and negative exponential moments. These are alternatives to repeated depth comparisons, with their own blur and visibility controls. They can produce light bleeding or overly dark transitions when poorly tuned.

Try **Variance (VSM)** with a small **Moment blur radius** first. Increase **Light bleed reduction** carefully. For ESM or EVSM, start with the supplied exponents rather than immediately using the maximum.

| Control | Default | Accepted values |
| --- | --- | --- |
| Positive exponent | 32 | Finite 0.1–40; shown for ESM/EVSM |
| Negative exponent | 8 | Finite 0.1–40; shown for EVSM |
| Minimum variance | 0.00001 | Finite 0.00000001–1; shown for VSM/EVSM |
| Light bleed reduction | 0.2 | Finite 0–0.999; shown for VSM/EVSM |
| Moment blur radius | 2 | Integer 0–8 texels |

The exponent bounds assume the new FP32 moment targets. Do not reinterpret these values as a safe configuration for an arbitrary half-float moment implementation. The existing Poisson comparison filter applies to Depth; it does not replace the moment blur.

## Deep opacity shadows

Choose **Deep (8 bins)** to accumulate opacity as optical depth along the shadow direction. It uses a fixed eight-bin approximation rather than an unbounded fragment list. The input is the material/object `BaseColorOpacity.a`, scaled by **Opacity multiplier**; the separate optical-depth limit bounds its contribution.

| Control | Default | Accepted values |
| --- | --- | --- |
| Opacity multiplier | 1 | Finite 0–16 |
| Maximum optical depth | 16 | Finite 0.01–32 |
| Deep maximum distance | 20,000 | Finite 0.01–1,000,000 world units |

Start with a small, layered scene whose material opacity is below one. An opaque object is not a useful demonstration of graded transparency. Eight bins limit depth precision, and this shadow representation does not add transparent surface rendering, refraction or a general order-independent transparency system.

Deep sampling also checks the receiver's positive distance along the current view direction against **Deep maximum distance**. Receivers outside that view range receive no Deep attenuation.

## Shadow volumes and budgets

Choose **Shadow Volume** with Single directional or Cube point projection. Begin with a closed outward-facing cube and a light outside it.

The topology builder requires opaque, closed, consistently outward-oriented static triangle geometry. Exact coincident positions can be welded across normal/UV seams. Open, degenerate, pinched or nonmanifold geometry fails explicitly; point lights on or inside the source mesh are rejected. Objects marked to skip shadow-volume updates are excluded. Analytic Water is not closed static volume geometry and uses its depth-shadow fallback.

**Geometry triangle budget** defaults to 20,000 source triangles and accepts integers 1–65,536. It is the existing `VolumeMaxTriangles` setting shared with Dual Paraboloid preparation; the file and console names remain unchanged. **Volume range** defaults to 20,000 and accepts finite 32–1,000,000 world units. Finite extrusion, camera clipping and scene scale affect the result; the setting does not request infinite shadow geometry.

Advanced processing admits at most **16 lights per view**. Additional targets are bounded to **192 MiB per light and 384 MiB per view**. Multiple atlas tiles, moment/blur targets and deep bins all contribute. A valid 1024-pixel setting can still exceed the resource budget for a particular light/projection combination.

The geometry cache holds at most **32 entries / 64 MiB**, and uncached CPU geometry preparation admits at most **131,072 input triangles per view**. These bounds are separate from the per-light Geometry triangle budget. Lowering resolution alone does not fix a topology or triangle-budget error. Lights that cannot be admitted retain existing shadows with a diagnostic. These limits do not impose a hard frame-time limit on driver allocation, CPU subdivision or shader compilation.

## Console and file compatibility

Console edits are owner-thread, validated session changes. Invalid numbers or enum values retain the previous complete advanced settings snapshot. Examples:

```text
r.shadow.projection 1
r.shadow.cascade.count 4
r.shadow.cascade.lambda 0.5
r.shadow.representation 3
r.shadow.moments.positive_exponent 32
r.shadow.moments.negative_exponent 8
r.shadow.moments.min_variance 1e-8
```

Representation values are `0=Depth`, `1=VSM`, `2=ESM`, `3=EVSM`, `4=Deep`, `5=Shadow Volume`. Directional projection is `0=Single`, `1=CSM`, `2=PSM`; point projection is `0=Cube`, `1=Dual Paraboloid`.

Other commands are `r.shadow.point_projection`, `r.shadow.resolution`, `r.shadow.max_distance`, `r.shadow.cascade.stabilize`, `r.shadow.caster_padding`, `r.shadow.moments.bleed_reduction`, `r.shadow.moments.blur_radius`, `r.shadow.deep.opacity_multiplier`, `r.shadow.deep.tau_max`, `r.shadow.deep.max_distance`, `r.shadow.volume.max_triangles` and `r.shadow.volume.range`. Use a command without a value to inspect its current value. The existing `r.shadow.filter` and `r.shadow.radius_texels` remain available.

Project manifests keep schema version 1 and store advanced defaults under `render.shadow.*`. Scene, prefab and selection documents keep version 1; an optional `light.shadow` object stores a complete per-light override. Old lights without that object inherit defaults, and saving an untouched light does not add an override. An optional entity `shadowCaster` boolean stores object casting; absence inherits the prototype flag, and explicit `false` remains distinct from inheritance. It is valid only for renderable objects and accepts exact JSON booleans. Unknown or partial override fields, nonfinite numbers, malformed integers and incompatible per-light combinations reject a load without replacing the current document or history. Older builds cannot consume the new optional shadow fields.

[← Scene features](scene-features.md) · [Manual index](README.md)
