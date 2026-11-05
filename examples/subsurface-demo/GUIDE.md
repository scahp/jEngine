# Subsurface diffusion controls

Open project.jproject: startup is the Skin receiver, not the last tested scene. All lengths use this scene's world units; these presets are approximate surface diffusion, with no hidden-surface transmission. The fixture uses one radiusScale6 and 64-pixel cap in all active cases to resolve the profiles at this camera scale.

Scenes retain actual material fields, caster policy and graph assets. off/global-zero additionally require disabling project SSS or setting its strength0; material-zero/thickness-zero retain enabled SSS and exercise real material neutral controls. masked-wax's graph reads a linear mask texture, a thickness scalar and a Diffusion Profile node; its scalar material fallback deliberately differs. skin-msaa4 combines real per-sample Deferred lighting and SSS; select4 samples in project settings when opening that material scene by itself.

Raw PNGs, camera-projected receivers, actual HDR target metrics, untouched metallic/emissive witnesses and all thresholds are in subsurface-gpu-report.json beside this fixture. GPU validation logs are owned by the native runner.
