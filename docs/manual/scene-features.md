# Scene features and baking

[Manual index](README.md) · [Material mapping nodes](material-mapping.md)

For a prepared scene, open the [scene-feature sample project](../../examples/scene-feature-demo/README.md).

## Configure environment, reflections and atmosphere

Open a project, then choose **Window → Environment / reflections / atmosphere**. The **Scene Features and Baking** window edits project settings. Features start disabled.

- **SH diffuse environment:** enable SH and select a project-relative `.jsh` asset. An empty asset uses uniform linear radiance `0.1`. Active diffuse GI replaces SH by default; enable **Combine with diffuse GI** only when you want both contributions. SH provides diffuse illumination rather than a visible sky image.
- **Pixel projected reflection:** use **Deferred** with stored GBuffer attachments and single-sample scene inputs. Configure one reflection plane as `nx,ny,nz,d`, where `dot(normal, worldPosition) = d`; `0,1,0,0` is the ground plane. Thickness and roughness cutoff control eligible surfaces. Missing or off-screen information uses fallback reflection.
- **Rayleigh / Mie atmosphere:** set **Meters per world unit** to match your scene scale and **Ground height** to its world-space Y. Follow the scene directional light, or disable that option and set the fallback direction toward the sun. Start with the default sample counts.

Click **Save and Apply** to validate, save `project.jproject` and apply at the next frame boundary. Invalid values retain the last valid settings. **Revert edits** reloads saved values. These controls configure raster rendering; atmosphere uses a ground-level single-scattering approximation.

## Bake an SH environment

1. Import an equirectangular HDR image into the project.
2. Under **Bake project assets**, enter its project-relative path in **HDR source** and an output such as `Assets/Lighting/Studio.jsh` in **SH output file**.
3. Click **Project HDR and use SH**. Completion normally selects the result, enables SH and saves the setting. If environment settings changed during baking, select the saved asset yourself and click **Save and Apply**.

The versioned `.jsh` stores nine linear RGB radiance SH coefficients. Repeat with the same output path to rebake.

## Bake Horizon data

Enter a height image in **Height source (R)** and a new folder such as `Assets/Bakes/Brick` in **Horizon output folder**. The red channel supplies normalized height. Choose **Bake resolution** (32–512), radius, physical height scale and baked texel size. Enable wrapping for a tiling height field, then click **Bake horizon layers and AO**.

The folder receives `layer0.png`, `layer1.png`, `ao.png` and `horizon-bake.json`. The two RGBA8 layers store eight horizon angles; alpha contains data. Use these as **linear data textures**, following the [Horizon material-node instructions](material-mapping.md). AO is separate.

Rebake into the same managed folder to refresh assets while preserving their registered IDs. A folder containing unrelated files is rejected. Both bakers use one asynchronous CPU job, accept inputs up to 4M pixels/64 MiB and require outputs inside the project's asset folder. **Cancel bake** is available during computation; final publication finishes before another job starts. Status messages report failures and output paths.

## Add and animate water

Choose **Add → Water**, select it in **Hierarchy** and edit **Analytic water surface** in **Inspector**. Adjust grid resolution, width, depth and base height. **Pond wave preset** restores four waves while keeping the base height; individual waves expose direction, amplitude, wavelength, speed, steepness and phase.

Distances use mesh-local units before the entity transform, speed uses local units per second, and phase uses radians. **Play** advances wave time; **Stop** restores the authored scene. Wave editing is disabled during Play and for locked entities. Save the scene to retain your water settings.

Forward/Deferred drawing, shadow maps and picking share the animated surface, with up to four waves. Deformation does not generate collision geometry or update ray-tracing hit geometry. Path Tracing selection is gated while raster-only water or mapping materials are present. PPR uses one configured plane, so it does not follow individual wave crests.

If Path Tracing is already active, new raster-only water/material publication is held with a diagnostic. Switch to Forward or Deferred to publish the authored changes.
