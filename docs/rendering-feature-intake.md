# Rendering feature intake

Reviewed on 2026-11-05; the selected batch builds on local jEngine `26eb307d` (including the earlier shared PCF module). This is a source and integration review. A source branch with a connected demo is not a claim of current Windows/macOS engine support.

## Prepared sources

| Source | Reference | Local checkout |
| --- | --- | --- |
| [Shadows](https://github.com/scahp/Shadows) | `master` / `87853997029283edfbbb60eaad03cf163c95f26c`, fetched and clean | `/Users/Shared/git/Shadows` |
| [PathTracingBSDF](https://github.com/scahp/jEngine/tree/PathTracingBSDF) | `4ca834ae6e7d339911d653a0bf56ef184ea268cd` | `/Users/Shared/git/jEngine-bsdf-prepare`, detached reference worktree |

Shadows and jEngine use the same MIT notice, Copyright (c) 2019 Choi Jae Ho. Existing engine components are sufficient for the first integration; no new dependency is needed. Reference checkouts remain unchanged. Historical branches are sources of algorithms, not replacements for current renderer, resource, material or platform code.

## Engine integration contract

The director discussed these boundaries with the core, graphics and editor agents:

- **Algorithm module:** explicit inputs, outputs, units and validation; independent of editor state, sample scenes and graphics API objects. Shared HLSL implements GPU algorithms; CPU helpers own settings, scheduling inputs or offline processing.
- **Renderer adapter:** existing passes and RHI own allocation, transitions, frame lifetime and owner-thread publication. Forward and Deferred consume shared modules where their required inputs are available. Path Tracing uses the shared material contract where appropriate, rather than borrowing raster visibility.
- **Editor adapter:** console, project settings and authoring feed the same validated configuration. Scene and Game use the same runtime settings. Asset metadata and material version changes need an explicit migration before new slots become engine features.
- **Capability and cost:** every feature declares required depth/normal/color/history/light data, supported pipelines, bounded memory/work, fallback and invalidation rules. An option or SDK declaration alone is insufficient.

Small modules can use the current pass structure. A new plugin registry, render graph or second scene database is not required for the work below. Those are separate architectural changes if a later feature establishes a concrete need.

## Added now: shared shadow filtering

**Poisson PCF** is the bounded first integration. `Renderer/Features/jShadowFiltering.h` validates a plain CPU configuration; `Resource/Shaders/hlsl/shadowfiltering.hlsl` exposes `SampleShadowVisibility2D` and `SampleShadowVisibilityCube`. Directional, spot and point shadow readers in Forward and Deferred all call it.

The module returns comparison visibility. Callers retain projection, reference-depth encoding, bias, reverse-Z and whether a light casts shadows. A validated per-view block carries mode and radius; there are no new render targets, readbacks, passes or queue waits. Cube offsets use a local directional basis: their texel radius is an angular approximation, and the basis has a pole transition rather than rotation-invariant filtering. Fixed sample positions avoid introducing frame noise. One-texel neutral maps, zero radius and Existing mode retain a single comparison lookup.

Open **Window > Project settings > Renderer defaults > Shadows**, select **Poisson PCF**, then save/apply the project settings. **Radius in texels** accepts finite values from 0 to 4. Saved defaults are shared by Scene and Game. The classic rendering panel exposes the same live settings. Console equivalents are `r.shadow.filter 1` and `r.shadow.radius_texels 1`.

The default is **Existing**, preserving current output and lookup cost. Poisson mode uses 16 comparison lookups for an affected shadow sample, so it is a quality/cost option rather than a performance improvement. This is filtering with a fixed texel footprint; it does not implement PCSS contact hardening. Path Tracing and HWRT direct-lighting rays bypass raster shadow filtering. Invalid console values fall back to Existing in the GPU snapshot; invalid persistent settings are rejected transactionally.

Source: [Shadows `shadow.glsl`](https://github.com/scahp/Shadows/blob/87853997029283edfbbb60eaad03cf163c95f26c/Shadows/Shaders/shadow.glsl), ring-disk sampling adapted to a fixed 16-sample kernel. The old OpenGL pipelines and resources were not imported.

## Selected implementation batch

The user selected **8, 9, 10, 11, 14 and 15**, with height mapping represented by reusable material nodes. The original conversation numbers remain stable below. Item 7 is included as the authorized UV/mapping foundation for 14 and 15.

| No. | Feature | Engine integration |
| --- | --- | --- |
| 7 | Parallax / POM | Typed UV, Texture Object, Texture Sample, Normal Sample and Channel nodes feed bounded Parallax/POM functions; Forward and Deferred compile the graph into material shader variants. |
| 8 | SH environment lighting | CPU HDR projection into a versioned `.jsh` radiance asset; shared diffuse irradiance evaluation; project settings and cancellable asset baker. |
| 9 | Pixel projected reflection | Deferred depth/color/normal input, integer projection scatter, reflection resolve, bounded hole fill and environment fallback; separate transient targets. |
| 10 | Atmospheric scattering | Rayleigh/Mie single scattering and scene transmittance after opaque lighting, with explicit scene-unit conversion and sample budgets. |
| 11 | Physical waves | Reusable analytic Gerstner CPU evaluator and matching four-wave raster deformation; Water scene entity, Inspector, Play timeline, shadow/picking deformation and conservative bounds. |
| 14 | Horizon mapping | Bounded eight-direction height-field baker and lossless linear assets; a material Horizon visibility output for primary directional light and diffuse environment AO. |
| 15 | Relief / interior mapping | Reusable Relief and Interior nodes, optional RG dual-depth inputs and bounded search/refinement. |

All scene features default off. Material v1 assets remain loadable; new graphs save as v2. Material node topology uses the existing shader cache, while numeric parameters and texture leases remain material data. CPU decoding/baking never creates graphics resources on workers; the frame owner publishes prepared results.

Usage: [Scene features and baking](manual/scene-features.md), [Material mapping nodes](manual/material-mapping.md). Validation and actual platform scope are recorded in the [implementation report](reports/selected-scene-features.md).

The atmospheric model is a ground-level approximation, not a planet/LUT model. PPR requires Deferred, stored GBuffer attachments and single-sample inputs; it reflects visible scene information and uses fallback for missing information. Water and UV/interior mapping do not create collision geometry or update ray-tracing hit geometry. Selecting Path Tracing is gated while these raster-only materials are present.

## Advanced shadow implementation batch

The user selected **1, 17, 18, 19, 20 and 21**. These now share a pure CPU math module, a validated settings contract, GPU helpers and an owner-thread renderer adapter. Forward and Deferred consume the same per-light result. Project defaults, light overrides, object caster flags, scene/prefab persistence and Undo use the existing editor workflow.

| No. | Feature | Delivered engine integration |
| --- | --- | --- |
| 1 | CSM restoration | One to four practical splits, texel stabilization, bounded distance/padding and an atlas in the current Vulkan/DX12 RHI. |
| 17 | VSM / ESM / EVSM | Scene/view-fitted directional maps, consistent linear Point/Spot depth, FP32 moments and bounded blur; actual receiver contrast verified against shadows OFF. |
| 18 | Shadow volumes | Welded closed-manifold geometry, finite capped extrusion, z-fail stencil and per-light visibility mask; explicit fallback for unsupported geometry. |
| 19 | Deep shadows | Eight additive optical-depth bins in two half-float targets, using authored scalar material opacity. |
| 20 | Dual-paraboloid point shadows | Two-hemisphere atlas with bounded conforming subdivision and seam sampling; cube fallback on invalid geometry or budget exhaustion. |
| 21 | Perspective shadow maps | Viewer-dependent post-perspective directional projection, virtual slide-back and reported fitted-orthographic fallback. |

Existing Single/Cube/Depth defaults retain the original path. Advanced resources and geometry preparation have explicit budgets. These are raster shadow features; Path Tracing and HWRT ray visibility retain their own evaluation. Windows backend integration and DXIL compilation are implemented, while physical Windows runtime validation is pending.

Usage: [Advanced shadows](manual/advanced-shadows.md). Evidence, performance scope and limitations: [implementation report](reports/advanced-shadows.md). Try the [saved sample project](../examples/advanced-shadow-demo/README.md).

## Selected SSS / MSAA / Hi-Z — Screen and human-reference verification

The user selected **12, 22 and 24**, including material-authored SSS properties and nodes. Native Vulkan verification on macOS passed **127 rendered frames / 183 GPU checks** across the generic Screen SSS, MSAA and Hi-Z groups, with zero Vulkan/Metal validation problems and preserved preferences. This proves those fixtures rather than the original Shadows Skin pipeline or a human-mesh comparison. Windows-targeted DXIL compilation passed; Windows native execution is unverified. No performance improvement is inferred from correctness checks.

| No. | Selected feature | Delivered engine integration |
| --- | --- | --- |
| 12 | Surface diffusion SSS | Verified Screen path: material weight/color/world thickness/eight profiles and graph/instance data feed Deferred direct+SH diffuse filtering, with existing SSGI/SurfelGI unchanged. The separately verified human reference adds explicit Object/World graph normals, original linear Spec/AO, Skin Kelemen material properties, UV diffusion and measured capture/transmission: 76 frames / 142 checks. Skin Kelemen requires Deferred; original pixel parity is not claimed. |
| 22 | Deferred per-sample MSAA | Validated 1x/2x/4x requests, capability-dependent applied count, multisample GBuffer/lighting/resolve integration and preserved subpass/memoryless requests. Forward falls back to 1x with a reason. |
| 24 | Hi-Z occlusion | Same-frame mesh depth, maximum pyramid and bounded indexed indirect arguments; conservative draw admission and direct-draw fallback. Hi-Z defaults off and rejects multisample coverage. |

SSS and Hi-Z default off, with MSAA requesting 1x. The [feature guide](manual/material-diffusion-msaa-hiz.md) documents exact material/profile controls, project/console keys and support limits; the [implementation report](reports/material-diffusion-msaa-hiz.md) records actual captures, readbacks and platform scope. SSS passed39frames/140checks, Hi-Z51/23 and MSAA37/20, including combined4x SSS, geometry-derived witnesses and actual per-sample lighting/resolve checks. Applied MSAA above1x explicitly bypasses Hi-Z. Runtime diagnostics remain separate from saved requests.

The [human Skin sample](../examples/subsurface-human-demo/README.md) and [reference audit](reports/skin-reference-audit.md) publish the separate **76-frame / 142-check** corrected native receipt, six actual images and six saved comparison projects, with zero validation problems and preserved preferences. UV transport isolates one shadow-enabled directional/point key with no diffuse GI/environment, explicit opaque graph coverage and Skin profile 0. Unsupported views and ambiguous eye/silhouette/missing-capture footprints keep Screen fallback locally. Source-normal basis, pre-scatter split and units are saved data; the provisional roughness scale is not a pixel-perfect source calibration.

## Remaining decisions

The nine items below remain unselected. Historical reference branches are not current engine feature claims; the selected Hi-Z integration is tracked separately above.

| No. | Feature / source branch | Recommended engine module and decision |
| --- | --- | --- |
| 2 | Forward+ / `ForwardPlus` | Shared bounded light grid for Forward/Deferred. Choose light limits and overflow policy. |
| 3 | Weighted OIT / `WeightOIT` | Transparency accumulation/revealage and material classification. Agree approximation and fallback. |
| 4 | BSDF / jEngine `PathTracingBSDF` | Shared sampling/evaluation/PDF library. Preserve current index/binding fixes and repair refraction math. |
| 5 | Russian Roulette / jEngine `PathTracingBSDF` | Unbiased integrator termination; repair throughput compensation and compare against a reference. |
| 6 | PCSS / `master` | Extend shared shadow filtering. Define light size, depth units and penumbra limits. |
| 13 | Cosmetic layers / `Physically-based-cosmetic` | Material layer optics, masks and thickness; requires the skin/shading contract. |
| 16 | Radiosity / `RadiosityCornellBox` | Bounded cancellable offline bake, or retain as research. Choose output asset and scene scope. |
| 23 | 2D SDF text / `SDF` | Optional scalable text atlas/rendering module; this source is not a 3D distance-field renderer. |
| 25 | Light-indexed rendering / `LightIndexedDeferredRendering` | **Prototype:** evaluate pixel light indices as another light-grid consumer; source renderer `Do` is empty. |

Nine numbered choices remain: **2, 3, 4, 5, 6, 13, 16, 23 and 25**. For general scenes, the next coherent decision group is **2 Forward+ and 3 OIT**. For Path Tracing quality, decide **4 BSDF and 5 Russian Roulette** together.

### CSM history and current runtime

CSM was added in shared jEngine/Shadows history by `3b7a12ba0c52` (2019-07-27, "Added Cascade shadow"), with later fixes including `11b6e3129b0b` and `d87f1e7af69e`. These commits are ancestors of jEngine main. Commit `797710bcf911` (2022-08-25, "Remove all files to merge vulkan api") deleted the previous pipeline, cascade light code and GLSL shaders before the Vulkan merge `e2d68554e187`. The prepared Shadows `master` still contains the old implementation.

At the earlier audit commit `f8ec7f64`, [`jDirectionalLight`](../jEngine/Scene/Light/jDirectionalLight.h) holds one shadow matrix, [`jSceneRenderTargets.cpp`](../jEngine/Renderer/jSceneRenderTargets.cpp) allocates a single-layer 2D directional shadow target, and [`directionallight_fs.hlsl`](../jEngine/Resource/Shaders/hlsl/directionallight_fs.hlsl) samples that matrix. The old `CreateCascadeDirectionalLight` factory remains declared, but its implementation/update bodies in [`jLight.cpp`](../jEngine/Scene/Light/jLight.cpp) are commented out. The SurfelGI Cascade controls are GI clipmaps, not directional-light CSM.

The selected advanced-shadow batch restores CSM through the current RHI and shared lighting bindings. The old factory remains historical; the active implementation is `Renderer/jAdvancedShadowPass.cpp` with reusable projection math in `Renderer/Features/jAdvancedShadowMath.cpp`. The earlier audit describes the pre-restoration state.

## Excluded from new-feature counts

| Item | Reason |
| --- | --- |
| Basic shadow maps, comparison sampling, normal mapping, PBR, light volumes | Current jEngine already has corresponding rendering paths. Poisson PCF extends the existing comparison path. |
| `sponza_atmosphericshadowing` (`691d7f8f`) | Atmospheric shadowing already exists in `jRenderer_AtmosphericShadowPass.cpp`; review any quality differences separately rather than importing a second implementation. |
| Bloom / tone mapping / adaptive luminance | Current post-processing already implements these effects. |
| Basic environment cube maps / IBL resources | Present in jEngine. SH projection is a separate missing feature; the current IBL final-addition policy needs separate review before re-enabling it. |
| `VulkanAPI`, `LightVolume`, `SphericalMapping` as standalone feature names | Backend/sample/infrastructure labels are not proof of a new missing engine feature; jEngine already has Vulkan, light volumes and environment mapping. |
| PBRT Fresnel patch from `PathTracingBSDF` as ready-to-merge code | Its `sin2ThetaI / eta * eta` cancels eta. Fix and validate the math before adopting it. |
| SDF compute registration alone | The confirmed SDF rendering path is 2D text. No source dispatch was found proving a complete general 3D SDF generator. |
| `LightIndexedDeferredRendering` (`02c0ac0b`) as a complete renderer | `jGame.cpp:432,487` demonstrates light-ID encoding/shading, but renderer `jPipeline.cpp:942` has an empty `Do`. Consider the pixel-index representation only as an alternative consumer of a shared LightGrid; packed RGBA8 capacity and precision need a separate design. |

## Source references for follow-up work

| Branch | Pinned tip |
| --- | --- |
| `master` | `87853997029283edfbbb60eaad03cf163c95f26c` |
| `PSM` | `4d37398ec18f7b8f8d2f29231dec7d6bb356a1b9` |
| `DualParaboloidShadowMap` | `ccfec06ca606fc56d0dbd31d7916c23770aafb95` |
| `ForwardPlus` | `8284b8669f4b14e846c1bcab017b7dc5bace062d` |
| `HiZOcclusion` | `02a852037ce40b3396faf1ad4f05f60085a2fdb5` |
| `WeightOIT` | `d2189b980c02bce5346ede431e8dd96ecdd3273c` |
| `sponza` | `9112cc5acab38bba695f3249affe0b98c74df24d` |
| `IrradianceMap` | `51b3ac66397580f586a50c8d882b2e1da794cdb0` |
| `AccurateAtmosphericScattering` | `0166142f257e11db0bed35d4f99eb1f63cabf530` |
| `WaveWithPhysicalModel` | `6ae2d30a0e35dd02761127ef419246e338f5c414` |
| `RadiosityCornellBox` | `2096b1608eccc45591bbb9edc03cfabca4d504e8` |
| `ParallaxMapping` | `e88416344d4a47e6043f6708e49a7df3f9ece91f` |
| `HorizonMapping` | `cfac2ac67b29b29e92dd1d091304f1e6ec94b4c8` |
| `ReliefMapping` | `9acb77540afcef71c0c871c7450725d8944a03f9` |
| `Skin` | `51249d3d4d9ea1c3320741077aaa5335b28c3418` |
| `Physically-based-cosmetic` | `529bc5a069fe918c8e47e2dec4768575eaee36ad` |
| `SDF` | `27784c060b02b11578dfdc9aa04cb5f895f2115c` |
| `MSAA_And_Antialiasing` | `dde92aeaa0f5b33f06e62348e7b9a1a84636dd53` |
| `LightIndexedDeferredRendering` | `02c0ac0b022bbb8a48160b4fe7b886c0c8c997c6` |

Re-read source at the pinned reference before implementing a selected feature. The historical Hi-Z and light-indexed branches were scoped prototypes; current Hi-Z integration is tracked above, while light-indexed rendering remains an undecided prototype.

## Earlier PCF validation and delivery

- macOS Debug incremental engine build passed, including the changed shared view layout and options. The later shader-fixture binding correction rebuilt only its affected object and linked successfully.
- `./build/macos-debug/jEngine --ci-rendering-features`: **88 checks passed**, including 70 real project-settings checks and shared-view/filter contracts. Save/reopen, default restoration, invalid/nonfinite inputs, failed writes, scene revision preservation and owner-thread rules were exercised without a GPU. The extra-argument form fails before engine initialization.
- **20 affected DXIL/SPIR-V compilations passed** across Forward, directional/point/spot Deferred, shadow enabled/disabled and reverse-Z variants. SPIR-V reflection matched the CPU view size of 368 bytes and filter offset of 352. Future normal shader CI also includes the three Deferred shadow readers.
- The choice table was checked at normal and 320-pixel widths. Table scrolling contains narrow-screen overflow. A local test bridge confirmed that changing checkboxes does not submit a request, and that submitting two selections includes exactly those items and the chosen integration approach. Actual conversation delivery requires the user's action in the chat surface; QA selections are not user authorization.
- The core agent additionally ran 17 standalone filter-settings cases and helper-only DXIL/SPIR-V compilation. An independent graphics review identified and verified the one-texel neutral-map fast path.

Local receipts are under `build/shadows-reference/verification.json`, `affected-shaders.json`, `feature-cpu.log` and `choice-ui.json`; build output is intentionally untracked. No new GPU image, GPU timing, Windows runtime result or full CI result is claimed. These are the earlier PCF receipts, not evidence for the selected batch above. The selected-batch report records its own validation. Implementation stays on the current local work branch; push, full Windows/macOS validation and main integration wait until the user finishes the batch.
