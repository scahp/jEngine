# Advanced shadow sample

Open **project.jproject** through **File → Open project**. It starts on **Scenes/deferred-csm4.jscene**. Use **Play** for the saved primary camera, or move the editor camera normally. The sample uses built-in cubes and Terrain, with no external asset downloads.

Use **File → Open scene** to select one of the 38 saved diagnostic scenes. For `forward-*` scenes, choose **Forward** beside the viewport; the other scenes use **Deferred**. The renderer choice is a project/session option rather than part of a scene file.

Each active shadow light stores its own settings override. Select the light and open **Inspector → Light → Override shadow settings** to change it. Changing project defaults will not replace that explicit override. The floor stores `shadowCaster: false`; change it under **Inspector → Shadow casting**. The intentional invalid-volume controls instead make the open floor a caster and should report a fallback.

[Scene list and special cases](GUIDE.md) · [Complete controls and limitations](../../docs/manual/advanced-shadows.md) · [Verified captures and measurements](../../docs/reports/advanced-shadows.md)

The scenes were saved and reopened through the actual authoring document during the 2026-11-05 native check: 157 rendered frames, 343 passed checks, zero Vulkan/Metal validation problems on Apple M4. They are diagnostic geometry, not an art scene. Deep uses two scalar-opacity layers; it does not implement transparent surface compositing. Volume requires closed opaque caster meshes.

Moment scenes now preserve visible receiver shadows. Directional and CSM cases use scene/view bounds; Point/Spot moment depth is linearized in producer and reader. Shadow-off controls share the same geometry and light settings; the regression report compares actual receiver pixels.
