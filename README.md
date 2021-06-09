# Shadows
Implementation of Shadows

A. ShadowVolume(CPU and GPU) : branch master (Blog https://scahp.tistory.com/20 https://scahp.tistory.com/21) <br/>
B. ShadowMap : branch master
  1. SSM
  2. PCF
  3. PCSS (Blog https://scahp.tistory.com/27)
  4. VSM (Blog https://scahp.tistory.com/18)
  5. ESM (Blog https://scahp.tistory.com/19)
  6. EVSM
  7. CSM (Directional Light Only) (Blog https://scahp.tistory.com/39)
  8. DeepShadowMap (Directional Light Only) (Blog https://scahp.tistory.com/9)
  9. Dual Parabolid : branch link https://github.com/scahp/Shadows/tree/DualParaboloidShadowMap (Blog https://scahp.tistory.com/7)
  10. PSM : branch link https://github.com/scahp/Shadows/tree/PSM (Blog https://scahp.tistory.com/44)
<img src="https://user-images.githubusercontent.com/6734453/64974161-3f25d600-d8e7-11e9-8a87-fbb5655da514.png" width="90%"></img>
<img src="https://user-images.githubusercontent.com/6734453/64978132-c8410b00-d8ef-11e9-81cd-ba1051e2bced.png" width="90%"></img>
<img src="https://user-images.githubusercontent.com/6734453/121390069-fa2db700-c987-11eb-9d9e-5a1eb458406a.gif" width="70%"></img>

C. SubsurfaceScattering (SSS Skin Rendering) : branch link https://github.com/scahp/Shadows/tree/Skin https://youtu.be/cWpoWrE-62A

<img src="https://user-images.githubusercontent.com/6734453/83333392-2f1a1f00-a2db-11ea-8bfe-c30db6e4ae93.png" width="90%"></img>

D. Progressive Refinement Radiosity : branch link https://github.com/scahp/Shadows/tree/RadiosityCornellBox
<img src="https://user-images.githubusercontent.com/6734453/121384475-432f3c80-c983-11eb-821a-46b16339c630.png" width="90%"></img>

E. Diffuse IrradianceMap with Spherical Harmonics : branch link https://github.com/scahp/Shadows/tree/IrradianceMap
<img src="https://user-images.githubusercontent.com/6734453/121385392-09126a80-c984-11eb-8144-067344411d6c.png" width="90%"></img>

F. Atmospheric Shadowing : branch link https://github.com/scahp/Shadows/tree/sponza_atmosphericshadowing
<img src="https://user-images.githubusercontent.com/6734453/121385788-627a9980-c984-11eb-99c8-3294c7e329ab.gif" width="90%"></img>

G. Pixel projected Reflection : branch link https://github.com/scahp/Shadows/tree/sponza
<img src="https://user-images.githubusercontent.com/6734453/121386330-dddc4b00-c984-11eb-9090-941b75b60a57.png" width="90%"></img>

H. Effective Water Simulation from Physical Models : branch link https://github.com/scahp/Shadows/tree/WaveWithPhysicalModel
<img src="https://user-images.githubusercontent.com/6734453/121386722-2a278b00-c985-11eb-990f-5e32e181c622.gif" width="90%"></img>

I. Physically-Based Cosmetic Rendering : branch link https://github.com/scahp/Shadows/tree/Physically-based-cosmetic
<img src="https://user-images.githubusercontent.com/6734453/121387071-5c38ed00-c985-11eb-88cb-2059ffa6a4e9.png" width="70%"></img>

J. Accurate atmospheric scattering : branch link https://github.com/scahp/Shadows/tree/AccurateAtmosphericScattering https://youtu.be/USrTUBpIxKQ
<img src="https://user-images.githubusercontent.com/6734453/121387508-b043d180-c985-11eb-882d-84d9dc28e45d.png" width="90%"></img>

K. ForwardPlus Rendering : branch link https://github.com/scahp/Shadows/tree/ForwardPlus
<img src="https://user-images.githubusercontent.com/6734453/121387806-eda85f00-c985-11eb-8218-61b731bcf210.gif" width="90%"></img>

L. Signed Distance Field : branch link https://github.com/scahp/Shadows/tree/SDF
<img src="https://user-images.githubusercontent.com/6734453/121388087-221c1b00-c986-11eb-867d-6aaefc3805e0.gif" width="90%"></img>

M. Light Indexed Deferred Rendering : branch link https://github.com/scahp/Shadows/tree/LightIndexedDeferredRendering
<img src="https://user-images.githubusercontent.com/6734453/121388410-6efff180-c986-11eb-8b7d-71a39896a8b3.gif" width="70%"></img>

N. Hi-Z Occlusion culling : branch link https://github.com/scahp/Shadows/tree/HiZOcclusion
<img src="https://user-images.githubusercontent.com/6734453/121389057-fb121900-c986-11eb-9fcb-0c713cbbf567.png" width="90%"></img>


References
1. https://github.com/TheRealMJP/Shadows
2. GPU-Pro4 DeepShadowMaps
3. RealTimeRendering 4th
4. https://developer.nvidia.com/gpugems/GPUGems/gpugems_ch11.html
5. https://developer.download.nvidia.com/books/HTML/gpugems/gpugems_ch12.html
6. https://kosmonautblog.wordpress.com/2017/03/25/shadow-filtering-for-pointlights/
7. http://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf
8. http://developer.download.nvidia.com/SDK/10/direct3d/Source/VarianceShadowMapping/Doc/VarianceShadowMapping.pdf
9. http://jankautz.com/publications/esm_gi08.pdf
10. http://advancedgraphics.marries.nl/presentationslides/13_exponential_shadow_maps.pdf
11. http://developer.download.nvidia.com/presentations/2008/GDC/GDC08_SoftShadowMapping.pdf
12. http://ogldev.atspace.co.uk/www/tutorial49/tutorial49.html
13. http://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf
14. https://developer.download.nvidia.com/books/HTML/gpugems/gpugems_ch09.html
15. http://ogldev.atspace.co.uk/www/tutorial40/tutorial40.html
16. https://www.slideshare.net/Mark_Kilgard/realtime-shadowing-techniques-shadow-volumes
17. https://foundationsofgameenginedev.com/
