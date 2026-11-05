# Material mapping nodes

[Manual index](README.md) · [Authoring tools](08-authoring-tools.md) · [Scene features and baking](scene-features.md)

Material mapping graphs evaluate surface color, normals and apparent depth in **Deferred and Forward**. Parallax, POM, Relief and Interior change texture sampling; Horizon supplies separate lighting visibility and ambient occlusion. They do not displace the mesh or create collision geometry.

## Open, connect and apply a graph

1. Select an unlocked object in **Hierarchy**. Open **Window → Material / Animation / Effects / Terrain → Material graph**, or click **Inspector → Material graph**.
2. Choose a type in **New node**, then click **Add node**. Click its title to edit its values below the canvas. Drag its title to move it.
3. Drag **Out** onto a compatible input, or click an input and select its source. The same popup offers **Disconnect**. **Show output** scrolls to **Material Output**.
4. Connect a scalar, RGB or RGBA value to **Base Color**. Connect the other outputs as needed. UV coordinates, texture objects and Horizon visibility have distinct types; they cannot be used as colors.
5. Set **Graph asset** to a project-relative path such as `Assets/Materials/Brick.jmaterial`. Click **Save and Apply**, then save the scene to persist the object's graph reference.

Use **Undo graph** and **Redo graph** for graph edits. Applying the graph updates the scene document through its normal history. Saving a graph writes an asset file; scene Undo does not roll back that file on disk. **Load graph** opens an existing asset. **Use as shared material** assigns the saved asset through the shared-material workflow.

For imported scenes with a material sidecar, **Scene material library → Source material** selects a source slot. **Save and apply to scene** writes that slot's graph and refreshes the meshes using it. Paths in this mode are relative to the imported scene's material-library root.

## Nodes and exact pins

The table uses the labels shown on the canvas. A question mark marks an optional input. A texture object is an asset reference; a texture sample is a numeric value read from that asset.

| Node | Inputs | Output and purpose |
| --- | --- | --- |
| Color | None | Constant RGB, edited with **Node color**. |
| Scalar | None | Constant scalar, edited with **Node value**. |
| Texture | None | Legacy color sample at the mesh's original UV; retains the old graph workflow. |
| Multiply / Add | A, B | Numeric arithmetic. Matching vector widths or scalar/vector combinations are supported; float2 cannot mix with RGB/RGBA. |
| UV | None | Mesh float2 UV with scale and offset. |
| Texture Object | None | Project-relative **Texture path** and **Texture data** encoding. Connect this to texture inputs. |
| Texture Sample | Texture, UV | RGBA sampled at the supplied coordinates. |
| Sample Normal | Texture, UV | Linear normal data decoded from an explicitly selected Tangent, Object or World space, then converted to signed tangent-space float3 for the material output. |
| Channel | Value | One component of a numeric input: R=0, G=1, B=2, A=3, within that input's actual width. |
| Parallax / POM / Relief | UV, Height | View-dependent float2 coordinates. Relief uses the **Height** pin for its depth map too. |
| Interior | UV, Color, Depth?, Backdrop? | RGBA from a virtual room, or dual-depth relief when **Depth?** is connected. |
| Horizon | UV, Layer 0, Layer 1, AO | A dedicated visibility/AO value; connect only to **Material Output → Horizon**. |

**Material Output → Normal** accepts a signed tangent-space RGB normal; use **Sample Normal** for encoded normal maps. An unconnected Normal uses `(0,0,1)`. Metallic and Roughness require scalars; use **Channel** to extract a component from a texture. Unconnected Metallic/Roughness keep the object's material values.

Use **Texture data → Color (sRGB)** for ordinary encoded color maps. Use **Linear (height / data)** for height, depth and Horizon data, and **Normal (linear)** for normal maps. The mapping and normal consumers read their data linearly; choosing Color for a height asset does not turn height into color. Interior's Color and optional Backdrop must use matching color encoding.

## Start with a POM material

Prepare a tiling color map and a matching grayscale height map inside the project. Start with a small height scale and a normally lit object viewed at an angle.

1. Add **UV**, two **Texture Object** nodes, **POM**, and **Texture Sample**.
2. Set one Texture Object to the color map and **Color (sRGB)**. Set the other to the height map and **Linear (height / data)**.
3. Connect `UV.Out → POM.UV` and `height Texture Object.Out → POM.Height`.
4. Connect `color Texture Object.Out → Texture Sample.Texture` and `POM.Out → Texture Sample.UV`.
5. Connect `Texture Sample.Out → Material Output.Base Color`, then **Save and Apply**.
6. If you have a matching normal map, add its Texture Object and **Sample Normal**. Feed the same `POM.Out` to its UV, then connect its output to **Normal**. Check **Flip normal Y** if the normal convention requires it.

The color and normal details now shift together as the view changes. The silhouette, picking surface, shadows and collision still use the mesh. Increasing the step count costs more texture reads; it does not add geometry.

## Controls, defaults and limits

These defaults are assigned by **Add node**. Values in saved files are validated too; nonfinite values and unsupported ranges are rejected.

| Node | Controls and defaults | Accepted range |
| --- | --- | --- |
| UV | **UV scale** `(1,1)`; **UV offset** `(0,0)` | UI controls span −10,000…10,000 per component. |
| Channel | **Channel (RGBA)** `0` | Integer within the input width; RGBA permits 0…3. |
| Sample Normal | **Normal space** Tangent; **Flip normal Y** enabled for legacy Tangent, off by default for Object/World; **Negate source X/Y/Z** off | Space Tangent=0, Object=1, World=2; each source-axis sign is +1 or −1. An explicit Y-flip choice is saved independently. |
| Parallax | **Height / depth scale** `0.05`; **Bounded steps** `4`; **Flip height V** off | Scale 0…1; integer steps 1…128. Refinement is reserved and must be zero. |
| POM | Scale `0.05`; steps `32`; **Refinements** `6`; **Flip height V** off | Scale 0…1; steps 1…128; integer refinements 0…8. |
| Relief | Scale `0.05`; steps `32`; refinements `6`; **Dual depth (R front / G back)** off | Same scale/step/refinement limits as POM; dual-depth on/off. |
| Interior | **Room tiling** `(1,1)`; **Room depth** `1`; **Room ambient** `1` | Each tiling component >0…256; depth 0.001…64; ambient 0…4. |
| Horizon | **Softness (radians)** `0.05`; **Ambient occlusion strength** `1`; **Horizon angle scale** `1` | Softness 0.001…π/2 radians; AO strength 0…1; angle scale 0…4. |

Parallax iteratively offsets the supplied UV using the centered red height channel. POM traces a normalized depth interval, then refines the first hit. Relief can use a single red channel, or front/back RG: **R stores `1 − frontDepth`, G stores backDepth**, both normalized to 0…1. A conventional color image is not a dual-depth asset.

Interior without Depth uses a box-room projection. Connecting an RG Depth asset selects the dual-depth recipe, with a bounded 128-step trace and six refinements. If Backdrop is absent, the Color texture also supplies the background. These recipes are raster surface illusions; they do not create traversable rooms, occluders or new mesh intersections.

For an object-space normal map, choose **Object** and the source's actual axis signs. The normal follows the full mesh transform, including its mesh-local transform, parent and nonuniform scale. **World** means the decoded direction is already in engine-world axes and stays world-fixed when the object rotates. A texture named “world normal” can instead contain a source-rest/object-space bake; confirm its source convention before choosing. UV V flip, source-axis signs and normal Y flip are separate settings. Existing saved Sample Normal nodes with no space metadata retain Tangent and their original Y flip. These graph normal modes do not change the native Inspector normal-texture slot's Tangent interpretation.

## Connect baked Horizon maps

First use [Scene features and baking](scene-features.md) to produce `layer0.png`, `layer1.png` and `ao.png` from a height image.

1. Add three **Texture Object** nodes and set all three to **Linear (height / data)**.
2. Add **UV** and **Horizon**. Connect the shared UV to **Horizon.UV**.
3. Connect the corresponding texture objects to **Layer 0**, **Layer 1** and **AO**.
4. Connect `Horizon.Out → Material Output.Horizon`. Keep the color graph connected to **Base Color** separately.
5. **Save and Apply** and inspect a surface under the scene's primary directional light. Start with the default softness, AO strength and angle scale.

The two RGBA layers hold eight directional horizon angles. **Alpha is the fourth angle in each layer, not transparency.** Keep all four channels and avoid color correction, sRGB conversion, premultiplication or lossy recompression. Directions advance at `2πk/8` in UV space: k=0 is +U, k=2 is +V, k=4 is −U and k=6 is −V. Each channel stores an angle in `[0,π/2]` divided by `π/2`. AO is stored separately as the mean of `cos²(horizonAngle)`.

Horizon compares the direction from the surface toward the primary directional light against the baked angles. It supplies directional visibility separately from ambient AO; it does not darken Base Color in advance. Point and spot lights do not receive this directional Horizon shadow. **Ambient occlusion strength** blends between no AO and the baked AO map.

## Data preparation, reimport and overrides

Linear/Normal PNG, TGA, BMP and JPEG (`.jpg` / `.jpeg`) mapping sources use a separate uncompressed RGBA8 preparation path. It preserves the decoded channels, including Horizon alpha, with one mip level and no automatic mip generation. Accepted files are nonempty and at most **32 MiB**, at most **8192 pixels per axis**, and at most **36,000,000 texels** (including an original 6000×6000 map). This path does not add precision beyond the source's decoded 8-bit channels. JPEG's source compression remains lossy; decoding adds no gamma conversion or further block compression for Linear/Normal consumers.

Up to two CPU decode jobs run concurrently. The owner thread uploads prepared resources and publishes a material only after its required data is ready. The retained decoded-image cache holds at most 64 entries / 256 MiB; decoder scratch, simultaneous decoded/copy buffers and GPU allocations are additional memory. A graph whose pinned data working set exceeds the preparation budget reports an error rather than partially replacing the material. Color textures and other formats keep the engine's existing loading policy.

Reimport the graph or its changed texture through the project's asset workflow. File identity and explicit invalidation prevent an older decode from replacing a newer generation. While preparation is pending, or if a new graph/resource fails, the existing material remains in use. Initial imports keep their original material until the graph is ready. Cancellation retires outstanding decode work without publishing it; a later explicit reimport can retry.

Per-object graph overrides remain separate from the shared asset. An enabled **Base Color** override replaces the graph's final RGB; it is not an extra tint multiply. Normal, Metallic, Roughness and Base Color Texture overrides affect their corresponding outputs. Texture alpha is multiplied by the material/object opacity once. Clear an override to see that graph output again.

## Saved graphs and supported rendering

Existing `.jmaterial` **version 1** graphs retain Color, Scalar, Texture, Multiply and Add with their previous evaluation rules. They support the original constant PBR / single color texture workflow. A graph using the appended typed nodes or Horizon saves as **version 2**; current builds load both versions. Older builds cannot read the new nodes.

Compilation validates the whole graph, including disconnected nodes, for cycles, missing references, pin types and bounded data. The limits are **256 nodes, eight compiled texture bindings, 64 float4 uniform slots and 64 KiB of generated HLSL**. Reusing one file through different texture nodes or semantics may consume more than one binding. Numeric values and filenames are separate from shader code, so those edits can reuse the same code variant; topology and sampling changes may need a new variant. The saved asset contains nodes and values, not an arbitrary HLSL program.

| Area | Current behavior |
| --- | --- |
| Deferred / Forward | Evaluate the generated mapping surface and Horizon outputs. |
| Path Tracing / hardware ray-tracing hit shaders | Do not evaluate these mapping nodes. Renderer selection and runtime publication guards reject raster-only surfaces; authored edits can remain saved until a raster renderer is used. |
| Geometry / collision / ray intersections | Remain the original mesh; mapping does not provide displacement, collision or room geometry. |
| Asset thumbnails / node value swatches | Basic CPU previews; inspect mapped shading in the Scene viewport. |
| Timing | Bounded tracing and CPU preparation do not bound driver allocation, texture upload, acceleration-structure work or first shader compilation time. |

If Apply fails, read the displayed graph/resource diagnostic, fix the referenced pin, path or limit, and retry. If the viewport still shows the previous material, also check pending imports, the active renderer and local overrides. A retained previous material is not evidence that the new graph has finished applying.

[← Scene features and baking](scene-features.md) · [Manual index](README.md)
