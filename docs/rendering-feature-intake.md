# Rendering feature intake

Reviewed on 2026-11-05 against jEngine `c96aeb24` plus the local changes described below. This is a source and integration review. A source branch with a connected demo is not a claim of current Windows/macOS engine support.

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

## Choices: missing features with connected source implementations

Rows describe code and call paths found in the source branches. Only Poisson PCF above is being implemented automatically. Everything in this table awaits the user's submitted choice; recommended approaches are proposals, not approvals.

| ID | Feature / source branch | Evidence in source | Reusable engine module and decision |
| --- | --- | --- | --- |
| `pcss` | Contact-hardening shadows / `master` | `jPipeline.h:343`, `Shaders/shadow.glsl:146`; blocker search, penumbra and filter path | Extend ShadowFiltering after agreeing physical light size and depth conversion; retain PCF fallback. |
| `csm` | Cascaded directional shadows / `master` | `jPipeline.cpp:330`, `gs_cascadeShadowMap.glsl`, cascade selection in `fs.glsl` | CPU CascadeViewBuilder + layered shadow allocation. Choose cascade count/range/split/stabilization; use per-cascade RHI passes rather than a geometry-shader dependency. Current jEngine has one directional matrix/layer. |
| `moment-shadows` | VSM / ESM / EVSM / `master` | `jPipeline.h:367`, `jPipeline.cpp:434`, moment generation/blur/evaluation shaders | ShadowRepresentation policy plus shared blur. Choose representation, precision, light-bleeding tolerance and memory budget; existing D16 maps cannot simply enable it. |
| `forward-plus` | Tiled light lists / `ForwardPlus` | `jGame.cpp:435,498`, `compute_forwardplus_lightculling.glsl`, 1024-index tile lists | Shared LightGrid builder and light evaluation for Forward/Deferred is recommended. Decide whether this replaces the current limited Forward light selection or becomes a separate renderer option. Bound tile allocation and overflow explicitly. |
| `hiz` | Hi-Z depth pyramid and test / `HiZOcclusion` | `jEngine.cpp:57,175,181`: depth mip chain and one cube's bounds; transform feedback and CPU readback | **Prototype, not a finished scene-wide GPU culler.** Reuse depth reduction in DepthPyramid + Visibility modules after defining camera cuts, first-frame visibility, dynamic bounds, bounded indirect draws and fallback. A new engine integration is required. |
| `oit` | Weighted blended transparency / `WeightOIT` | `jGame.cpp:400,515`, accumulation/revealage blending and final composition | Transparency module after opaque lighting, with material blend classification. Choose weighted approximation and sorting fallback; it does not solve every glass/refraction case. |
| `deferred-msaa` | Deferred per-sample MSAA / `MSAA_And_Antialiasing` | `jGame.cpp:129`, `jPostProcess.cpp:174`, `fs_deepshadow.glsl:54` edge/sample shading | Extend GBuffer sample layout and lighting/resolve policy. General MSAA state already exists; missing per-sample Deferred shading needs a quality/performance target and Vulkan/MoltenVK-compatible sampling. |
| `parallax` | Height-based UV displacement / `ParallaxMapping` | `jGame.cpp:320`, `Shaders/fs_parallaxmapping.glsl:39` | SurfaceUV shader library + height texture/scale/step material data. Agree Forward/Deferred/PT UV behavior and normal/tangent convention. Normal mapping already exists. |
| `bsdf` | BSDF sampling/evaluation and mixed PDF / jEngine `PathTracingBSDF` | `34eb4fba`, `24a8475e`, refraction fixes `1b9abb53` / `5e056885` | Shared BSDF shader library under the current path tracer and material layout. Integrate refraction evaluation with the PDF change; preserve current packed-index and binding fixes. |
| `roulette` | Russian Roulette / jEngine `PathTracingBSDF` | `5b6622ab`, option/UBO/raygen path | Path-integrator termination policy with correct accumulated-throughput compensation and an unbiased reference comparison. The branch's weight update must be repaired before use. |
| `ppr` | Pixel projected reflections / `sponza` | `Code/Renderer/jDeferredRenderer.cpp:342,531`, registered projection/resolve compute shaders | Reflection module consumes scene depth/color/normals. Agree plane/surface eligibility and environment fallback; screen coverage cannot provide arbitrary off-screen reflections. |
| `sh` | SH diffuse environment irradiance / `IrradianceMap` | `jGame.cpp:295,477`, `compute_Gen_SH_Llm.glsl`, `IrrSHEnv_fs.glsl` | SH projection/evaluation library + versioned environment asset. Choose offline projection versus runtime update; eliminate the old synchronous compute readback. Coordinate IBL/SurfelGI indirect-light ownership. |
| `sky` | Atmospheric scattering / `AccurateAtmosphericScattering` | `jGame.cpp:292,364,389`, ground/sky shaders for camera inside/outside atmosphere | Atmosphere module with explicit world units, sun parameters and sky/environment outputs. Decide whether planetary scale is wanted or a compact sky model is sufficient. Existing atmospheric shadowing is a different effect. |
| `water` | Physical wave water / `WaveWithPhysicalModel` | `jGame.cpp:273,945,1087`, geometric and texture wave shaders | Wave simulation/state + Water surface renderer/material. Agree world units, time ownership, reflection/refraction and bounds; separate CPU wave evaluation for gameplay from GPU surface shading. |
| `sss` | Skin diffusion / `Skin` | `jGame.cpp:435,731`, UV irradiance/blur/final skin shading | SSS module + diffusion-profile asset. Choose UV-space source approach or screen-space approach; require a material model and explicit diffuse-light ownership. A serialized subsurface property alone is not runtime SSS. |
| `cosmetics` | Layered cosmetic optics / `Physically-based-cosmetic` | `jGame.cpp:902`, `skin_final_fs.glsl:134` | Reusable layer reflection/transmission evaluation, then Skin profile integration. Agree coefficients, layer thickness/masks and authoring workflow; existing clearcoat is not equivalent. |
| `horizon` | Height-field horizon self-shadow / `HorizonMapping` | `jGame.cpp:171,484`, `fs_horizonmapping.glsl:60` | Asset bake tool + material shadow evaluation. Define horizon texture encoding and invalidate on height changes; replace CPU PPL and power-of-two-only wrapping assumptions. Existing screen AO is not equivalent. |
| `relief` | Relief / dual-depth interior mapping / `ReliefMapping` | `jGame.cpp:340`, `fs_dualdepthrelief_interiormap.glsl` | Specialized SurfaceUV/Interior material functions with bounded steps. Choose basic relief versus dual-depth interiors and required assets. Raster UV displacement does not change collision or RT geometry. |
| `radiosity` | Progressive refinement radiosity / `RadiosityCornellBox` | `jGame.cpp:214,239,246,247,256`, `Radiosity` implementation | Offline bake module with bounded cancellable CPU work and a versioned result asset, or retain as a research scene. Do not run the sample's whole-scene solve in the editor frame loop. |
| `shadow-volume` | CPU/GPU stencil shadow volumes / `master` | `jPipeline.cpp:708`, CPU extrusion and GPU geometry-shader paths | Mesh adjacency/extrusion + stencil visibility module. Choose a portable CPU path and intended use case before adding another shadow representation. |
| `deep-shadow` | Deep shadow transmittance / `master` | `jPipeline.cpp:598`, linked-list draw/sort/link/resolve | Transparent-shadow/transmittance module. Needs an actual hair/volume use case, overflow policy and GPU memory budget; independent from stencil shadow volumes. |
| `dual-paraboloid` | Dual-paraboloid point shadows / `DualParaboloidShadowMap` | `jPipeline.cpp:56`, dual-paraboloid projection/generation/shading | Optional ShadowProjection strategy using two hemispheres. Agree seam/warp quality and portable geometry generation versus current six-face cube maps. |
| `psm` | Perspective shadow maps / `PSM` | `jGame.cpp:333`, `jPipeline.cpp:931`, PSM generation/shading | ShadowProjection strategy with camera-dependent warp and fallback for degenerate view/light alignment. The mock-camera demo must be separated from engine cameras. |
| `sdf-text` | 2D SDF font/text rendering / `SDF` | `jGame.cpp:310`, `tex_ui_sdf_fs.glsl` | Optional text atlas/tooling module when scalable text is needed. This source path is 2D font rendering, not a 3D scene/mesh distance-field feature. |

Recommended first decisions are CSM, a shared tiled light grid, Hi-Z and weighted OIT for general scenes, then the BSDF correction for path tracing. Material/sky/water modules should follow their concrete asset and scene requirements. This order is a recommendation; no unchecked feature is authorized automatically.

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

## Validation and delivery

- macOS Debug incremental engine build passed, including the changed shared view layout and options. The later shader-fixture binding correction rebuilt only its affected object and linked successfully.
- `./build/macos-debug/jEngine --ci-rendering-features`: **88 checks passed**, including 70 real project-settings checks and shared-view/filter contracts. Save/reopen, default restoration, invalid/nonfinite inputs, failed writes, scene revision preservation and owner-thread rules were exercised without a GPU. The extra-argument form fails before engine initialization.
- **20 affected DXIL/SPIR-V compilations passed** across Forward, directional/point/spot Deferred, shadow enabled/disabled and reverse-Z variants. SPIR-V reflection matched the CPU view size of 368 bytes and filter offset of 352. Future normal shader CI also includes the three Deferred shadow readers.
- The choice table was checked at normal and 320-pixel widths. Table scrolling contains narrow-screen overflow. A local test bridge confirmed that changing checkboxes does not submit a request, and that submitting two selections includes exactly those items and the chosen integration approach. Actual conversation delivery requires the user's action in the chat surface; QA selections are not user authorization.
- The core agent additionally ran 17 standalone filter-settings cases and helper-only DXIL/SPIR-V compilation. An independent graphics review identified and verified the one-texel neutral-map fast path.

Local receipts are under `build/shadows-reference/verification.json`, `affected-shaders.json`, `feature-cpu.log` and `choice-ui.json`; build output is intentionally untracked. No new GPU image, GPU timing, Windows runtime result or full CI result is claimed. Implementation stays on the current local work branch; push, full Windows/macOS validation and main integration wait until the user finishes the batch.
