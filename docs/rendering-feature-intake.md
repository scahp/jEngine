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

## Remaining decisions

No item below is authorized by this batch. Hi-Z and light-indexed shading remain prototypes in their reference branches.

| No. | Feature / source branch | Recommended engine module and decision |
| --- | --- | --- |
| 1 | Cascaded shadows / `master` | Cascade view builder and per-cascade RHI passes. Choose counts, splits, distance and stabilization. |
| 2 | Forward+ / `ForwardPlus` | Shared bounded light grid for Forward/Deferred. Choose light limits and overflow policy. |
| 3 | Weighted OIT / `WeightOIT` | Transparency accumulation/revealage and material classification. Agree approximation and fallback. |
| 4 | BSDF / jEngine `PathTracingBSDF` | Shared sampling/evaluation/PDF library. Preserve current index/binding fixes and repair refraction math. |
| 5 | Russian Roulette / jEngine `PathTracingBSDF` | Unbiased integrator termination; repair throughput compensation and compare against a reference. |
| 6 | PCSS / `master` | Extend shared shadow filtering. Define light size, depth units and penumbra limits. |
| 12 | Skin SSS / `Skin` | Diffusion profile and shared scattering pass. Choose texture-space or screen-space evaluation. |
| 13 | Cosmetic layers / `Physically-based-cosmetic` | Material layer optics, masks and thickness; requires the skin/shading contract. |
| 16 | Radiosity / `RadiosityCornellBox` | Bounded cancellable offline bake, or retain as research. Choose output asset and scene scope. |
| 17 | VSM / ESM / EVSM / `master` | Moment shadow representation and blur. Choose precision, memory and light-bleeding tolerance. |
| 18 | Shadow volumes / `master` | Adjacency/extrusion and stencil visibility. Choose a portable path and intended use. |
| 19 | Deep shadows / `master` | Transparent transmittance module. Define hair/volume use, overflow and memory budgets. |
| 20 | Dual-paraboloid shadows / `DualParaboloidShadowMap` | Optional point-light projection strategy; define hemisphere seam/warp quality. |
| 21 | Perspective shadow maps / `PSM` | Camera-dependent shadow warp with a robust degeneracy fallback. |
| 22 | Deferred per-sample MSAA / `MSAA_And_Antialiasing` | Multisample GBuffer/lighting/resolve policy; set a quality/cost target. |
| 23 | 2D SDF text / `SDF` | Optional scalable text atlas/rendering module; this source is not a 3D distance-field renderer. |
| 24 | Hi-Z / `HiZOcclusion` | **Prototype:** reusable depth pyramid, followed by bounded visibility/indirect-draw integration. |
| 25 | Light-indexed rendering / `LightIndexedDeferredRendering` | **Prototype:** evaluate pixel light indices as another light-grid consumer; source renderer `Do` is empty. |

For general scenes, the next coherent group is **1 CSM, 2 Forward+ and 3 OIT**, followed by a separately designed Hi-Z visibility module. For Path Tracing quality, decide **4 BSDF and 5 Russian Roulette** together.

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

Re-read source at the pinned reference before implementing a selected feature. Hi-Z and light-indexed shading are explicitly scoped prototypes; the table does not treat them as completed engine culling or renderer modules.

## Earlier PCF validation and delivery

- macOS Debug incremental engine build passed, including the changed shared view layout and options. The later shader-fixture binding correction rebuilt only its affected object and linked successfully.
- `./build/macos-debug/jEngine --ci-rendering-features`: **88 checks passed**, including 70 real project-settings checks and shared-view/filter contracts. Save/reopen, default restoration, invalid/nonfinite inputs, failed writes, scene revision preservation and owner-thread rules were exercised without a GPU. The extra-argument form fails before engine initialization.
- **20 affected DXIL/SPIR-V compilations passed** across Forward, directional/point/spot Deferred, shadow enabled/disabled and reverse-Z variants. SPIR-V reflection matched the CPU view size of 368 bytes and filter offset of 352. Future normal shader CI also includes the three Deferred shadow readers.
- The choice table was checked at normal and 320-pixel widths. Table scrolling contains narrow-screen overflow. A local test bridge confirmed that changing checkboxes does not submit a request, and that submitting two selections includes exactly those items and the chosen integration approach. Actual conversation delivery requires the user's action in the chat surface; QA selections are not user authorization.
- The core agent additionally ran 17 standalone filter-settings cases and helper-only DXIL/SPIR-V compilation. An independent graphics review identified and verified the one-texel neutral-map fast path.

Local receipts are under `build/shadows-reference/verification.json`, `affected-shaders.json`, `feature-cpu.log` and `choice-ui.json`; build output is intentionally untracked. No new GPU image, GPU timing, Windows runtime result or full CI result is claimed. These are the earlier PCF receipts, not evidence for the selected batch above. The selected-batch report records its own validation. Implementation stays on the current local work branch; push, full Windows/macOS validation and main integration wait until the user finishes the batch.
