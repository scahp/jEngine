# Infinite head scan attribution

**Infinite, 3D Head Scan — Lee Perry-Smith / Infinite-Realities.** Based on work at [Triplegangers](https://www.triplegangers.com/). The head scan and accompanying diffuse, normal and specular/AO maps are credited under [Creative Commons Attribution 3.0 Unported](https://creativecommons.org/licenses/by/3.0/), independently of jEngine's MIT code license.

Original source: http://www.ir-ltd.net/infinite-3d-head-scan-released. Published attribution: [Pixar RenderMan, Photorealistic Head](https://renderman.pixar.com/photorealistic-head) and [Unity 4.3 linear-lighting manual](https://docs.unity.cn/430/Documentation/Manual/LinearLighting.html).

The OBJ, `Infinite-Level_02.jpg`, `Infinite-Level_02_World_NoSmoothUV.jpg` and `SpecularAOMap.png` were copied byte-for-byte from the user's [Shadows Skin branch](https://github.com/scahp/Shadows/tree/51249d3d4d9ea1c3320741077aaa5335b28c3418/Shadows/SkinData). Provenance and SHA-256 digests are recorded in [provenance.json](provenance.json). The MTL adapter retains the diffuse filename and opaque defaults while removing the ambiguous bump binding. The material graph explicitly decodes the source-rest/object normal as linear data with source-axis signs `(+1,+1,-1)`; it is not assigned to the native Tangent normal slot. The linear specular/AO map uses R for specular weight, G for Beckmann roughness and B for diffuse occlusion. Geometry and source maps are unchanged.

Keep this attribution and license link when copying the sample assets. No additional rights or endorsement are implied.
