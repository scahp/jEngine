# Studio sample

A small, original authoring project with 12 entities: colored primitives, a checker-textured pyramid, a sculpted floor, two lights, seeded sparks, keyed motion and a primary camera.

## Open and explore

1. Build jEngine, then choose **File → Open project** and open this folder's `project.jproject`.
2. Select **Studio sample** in Hierarchy and press **F** to frame the scene. Select individual objects to inspect their transforms and materials.
3. Click **Play** to use **Studio camera**, animate **Animated satellite** and run **Golden sparks**. Click **Stop** to restore the edit scene and camera.

You can also launch from the engine's `jEngine/` asset directory:

```text
<path-to-jEngine-executable> --project <absolute-path-to>/examples/authoring-demo/project.jproject
```

## Try an edit

- Select **Coral box** and change its base color or roughness in Inspector. Undo with **Ctrl+Z**.
- Select **Animated satellite**, open **Window → Material / Animation / Effects / Terrain**, and choose **Animation**. Play, then scrub **Time** to preview its four-second keyed motion.
- Select **Sculptable floor** in the same tools window and choose **Terrain / Navigation**. Adjust the brush and click **Apply brush**; Undo restores the previous geometry.
- Use **File → Save As** to keep the original sample while making a variant.

## Files and defaults

`Scenes/Studio.jscene` is the startup scene. `Assets/Models/` contains the original OBJ/MTL pyramid; `Assets/Textures/StudioChecker.png` is its original checker texture. Keep the adjacent `.jasset` files with their sources to retain stable asset IDs.

Deferred rendering is enabled. SurfelGI, inline rays, SSGI and bloom start disabled, so exploring this scene does not require ray-query support. Camera bindings use the engine defaults. This sample demonstrates editor authoring and preview; it does not add physics or game logic.

All scene data, geometry and textures were created for this sample and are provided under the repository's [MIT license](../../LICENSE).
