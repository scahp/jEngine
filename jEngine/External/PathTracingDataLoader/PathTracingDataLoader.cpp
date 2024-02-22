/*

Copyright (c) 2018 Miles Macklin

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgement in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

*/

/*
    This is a modified version of the original code
    Link to original code: https://github.com/mmacklin/tinsel
*/

#include "pch.h"
#include <cstring>
#include "PathTracingDataLoader.h"
#include "GLTFLoader.h"

namespace GLSLPT
{
    static const int kMaxLineLength = 2048;

    bool LoadSceneFromFile(const std::string& filename, jPathTracingLoadData* scene, jRenderOptions& renderOptions)
    {
        FILE* file;
        fopen_s(&file, filename.c_str(), "r");

        if (!file)
        {
            printf("Couldn't open %s for reading\n", filename.c_str());
            return false;
        }

        printf("Loading Scene..\n");

        struct MaterialData
        {
            Material mat;
            int id;
        };

        std::map<std::string, MaterialData> materialMap;
        std::vector<std::string> albedoTex;
        std::vector<std::string> metallicRoughnessTex;
        std::vector<std::string> normalTex;
        std::string path = filename.substr(0, filename.find_last_of("/\\")) + "/";

        int materialCount = 0;
        char line[kMaxLineLength];

        //Defaults
        Material defaultMat;
        scene->AddMaterial(defaultMat);

        while (fgets(line, kMaxLineLength, file))
        {
            // skip comments
            if (line[0] == '#')
                continue;

            // name used for materials and meshes
            char name[kMaxLineLength] = { 0 };

            //--------------------------------------------
            // Material

            if (sscanf_s(line, " material %s", name, (int32)sizeof(name)) == 1)
            {
                Material material;
                char albedoTexName[100] = "none";
                char metallicRoughnessTexName[100] = "none";
                char normalTexName[100] = "none";
                char emissionTexName[100] = "none";
                char alphaMode[20] = "none";
                char mediumType[20] = "none";

                while (fgets(line, kMaxLineLength, file))
                {
                    // end group
                    if (strchr(line, '}'))
                        break;

                    sscanf_s(line, " color %f %f %f", &material.baseColor.x, &material.baseColor.y, &material.baseColor.z);
                    sscanf_s(line, " opacity %f", &material.opacity);
                    sscanf_s(line, " alphamode %s", alphaMode, (int32)sizeof(alphaMode));
                    sscanf_s(line, " alphacutoff %f", &material.alphaCutoff);
                    sscanf_s(line, " emission %f %f %f", &material.emission.x, &material.emission.y, &material.emission.z);
                    sscanf_s(line, " metallic %f", &material.metallic);
                    sscanf_s(line, " roughness %f", &material.roughness);
                    sscanf_s(line, " subsurface %f", &material.subsurface);
                    sscanf_s(line, " speculartint %f", &material.specularTint);
                    sscanf_s(line, " anisotropic %f", &material.anisotropic);
                    sscanf_s(line, " sheen %f", &material.sheen);
                    sscanf_s(line, " sheentint %f", &material.sheenTint);
                    sscanf_s(line, " clearcoat %f", &material.clearcoat);
                    sscanf_s(line, " clearcoatgloss %f", &material.clearcoatGloss);
                    sscanf_s(line, " spectrans %f", &material.specTrans);
                    sscanf_s(line, " ior %f", &material.ior);
                    sscanf_s(line, " albedotexture %s", albedoTexName, (int32)sizeof(albedoTexName));
                    sscanf_s(line, " metallicroughnesstexture %s", metallicRoughnessTexName, (int32)sizeof(metallicRoughnessTexName));
                    sscanf_s(line, " normaltexture %s", normalTexName, (int32)sizeof(normalTexName));
                    sscanf_s(line, " emissiontexture %s", emissionTexName, (int32)sizeof(emissionTexName));
                    sscanf_s(line, " mediumtype %s", mediumType, (int32)sizeof(mediumType));
                    sscanf_s(line, " mediumdensity %f", &material.mediumDensity);
                    sscanf_s(line, " mediumcolor %f %f %f", &material.mediumColor.x, &material.mediumColor.y, &material.mediumColor.z);
                    sscanf_s(line, " mediumanisotropy %f", &material.mediumAnisotropy);
                }

                // Albedo Texture
                if (strcmp(albedoTexName, "none") != 0)
                    material.baseColorTexId = (float)scene->AddTexture(path + albedoTexName);

                // MetallicRoughness Texture
                if (strcmp(metallicRoughnessTexName, "none") != 0)
                    material.metallicRoughnessTexID = (float)scene->AddTexture(path + metallicRoughnessTexName);

                // Normal Map Texture
                if (strcmp(normalTexName, "none") != 0)
                    material.normalmapTexID = (float)scene->AddTexture(path + normalTexName);

                // Emission Map Texture
                if (strcmp(emissionTexName, "none") != 0)
                    material.emissionmapTexID = (float)scene->AddTexture(path + emissionTexName);

                // AlphaMode
                if (strcmp(alphaMode, "opaque") == 0)
                    material.alphaMode = AlphaMode::Opaque;
                else if (strcmp(alphaMode, "blend") == 0)
                    material.alphaMode = AlphaMode::Blend;
                else if (strcmp(alphaMode, "mask") == 0)
                    material.alphaMode = AlphaMode::Mask;

                // MediumType
                if (strcmp(mediumType, "absorb") == 0)
                    material.mediumType = MediumType::Absorb;
                else if (strcmp(mediumType, "scatter") == 0)
                    material.mediumType = MediumType::Scatter;
                else if (strcmp(mediumType, "emissive") == 0)
                    material.mediumType = MediumType::Emissive;

                // add material to map
                if (materialMap.find(name) == materialMap.end()) // New material
                {
                    int id = scene->AddMaterial(material);
                    materialMap[name] = MaterialData{ material, id };
                }
            }

            //--------------------------------------------
            // Light

            if (strstr(line, "light"))
            {
                Light light;
                Vector v1, v2;
                char lightType[20] = "none";

                while (fgets(line, kMaxLineLength, file))
                {
                    // end group
                    if (strchr(line, '}'))
                        break;

                    sscanf_s(line, " position %f %f %f", &light.position.x, &light.position.y, &light.position.z);
                    sscanf_s(line, " emission %f %f %f", &light.emission.x, &light.emission.y, &light.emission.z);

                    sscanf_s(line, " radius %f", &light.radius);
                    sscanf_s(line, " v1 %f %f %f", &v1.x, &v1.y, &v1.z);
                    sscanf_s(line, " v2 %f %f %f", &v2.x, &v2.y, &v2.z);
                    sscanf_s(line, " type %s", lightType, (int32)sizeof(lightType));
                }

                if (strcmp(lightType, "quad") == 0)
                {
                    light.type = LightType::RectLight;
                    light.u = v1 - light.position;
                    light.v = v2 - light.position;
                    light.area = Vector::CrossProduct(light.u, light.v).Length();
                }
                else if (strcmp(lightType, "sphere") == 0)
                {
                    light.type = LightType::SphereLight;
                    light.area = 4.0f * PI * light.radius * light.radius;
                }
                else if (strcmp(lightType, "distant") == 0)
                {
                    light.type = LightType::DistantLight;
                    light.area = 0.0f;
                }

                scene->AddLight(light);
            }

            //--------------------------------------------
            // Camera

            if (strstr(line, "camera"))
            {
                Matrix xform;
                Vector position;
                Vector lookAt;
                float fov;
                float aperture = 0, focalDist = 1;
                bool matrixProvided = false;

                while (fgets(line, kMaxLineLength, file))
                {
                    // end group
                    if (strchr(line, '}'))
                        break;

                    sscanf_s(line, " position %f %f %f", &position.x, &position.y, &position.z);
                    sscanf_s(line, " lookat %f %f %f", &lookAt.x, &lookAt.y, &lookAt.z);
                    sscanf_s(line, " aperture %f ", &aperture);
                    sscanf_s(line, " focaldist %f", &focalDist);
                    sscanf_s(line, " fov %f", &fov);

                    if (sscanf_s(line, " matrix %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
                        &xform.m[0][0], &xform.m[1][0], &xform.m[2][0], &xform.m[3][0],
                        &xform.m[0][1], &xform.m[1][1], &xform.m[2][1], &xform.m[3][1],
                        &xform.m[0][2], &xform.m[1][2], &xform.m[2][2], &xform.m[3][2],
                        &xform.m[0][3], &xform.m[1][3], &xform.m[2][3], &xform.m[3][3]
                    ) != 0)
                        matrixProvided = true;
                }

                if (matrixProvided)
                {
                    Vector forward = Vector(xform.m[2][0], xform.m[2][1], xform.m[2][2]);
                    position = Vector(xform.m[3][0], xform.m[3][1], xform.m[3][2]);
                    lookAt = position + forward;
                }


                // todo
                scene->AddCamera(position, lookAt, fov);
                //scene->camera->aperture = aperture;
                //scene->camera->focalDist = focalDist;
            }

            //--------------------------------------------
            // Renderer

            if (strstr(line, "renderer"))
            {
                char envMap[200] = "none";
                char enableRR[10] = "none";
                char enableAces[10] = "none";
                char openglNormalMap[10] = "none";
                char hideEmitters[10] = "none";
                char transparentBackground[10] = "none";
                char enableBackground[10] = "none";
                char independentRenderSize[10] = "none";
                char enableTonemap[10] = "none";
                char enableRoughnessMollification[10] = "none";
                char enableVolumeMIS[10] = "none";
                char enableUniformLight[10] = "none";

                while (fgets(line, kMaxLineLength, file))
                {
                    // end group
                    if (strchr(line, '}'))
                        break;

                    sscanf_s(line, " envmapfile %s", envMap, (int32)sizeof(envMap));
                    sscanf_s(line, " resolution %d %d", &renderOptions.renderResolution.x, &renderOptions.renderResolution.y);
                    sscanf_s(line, " windowresolution %d %d", &renderOptions.windowResolution.x, &renderOptions.windowResolution.y);
                    sscanf_s(line, " envmapintensity %f", &renderOptions.envMapIntensity);
                    sscanf_s(line, " maxdepth %i", &renderOptions.maxDepth);
                    sscanf_s(line, " maxspp %i", &renderOptions.maxSpp);
                    sscanf_s(line, " tilewidth %i", &renderOptions.tileWidth);
                    sscanf_s(line, " tileheight %i", &renderOptions.tileHeight);
                    sscanf_s(line, " enablerr %s", enableRR, (int32)sizeof(enableRR));
                    sscanf_s(line, " rrdepth %i", &renderOptions.RRDepth);
                    sscanf_s(line, " enabletonemap %s", enableTonemap, (int32)sizeof(enableTonemap));
                    sscanf_s(line, " enableaces %s", enableAces, (int32)sizeof(enableAces));
                    sscanf_s(line, " texarraywidth %i", &renderOptions.texArrayWidth);
                    sscanf_s(line, " texarrayheight %i", &renderOptions.texArrayHeight);
                    sscanf_s(line, " openglnormalmap %s", openglNormalMap, (int32)sizeof(openglNormalMap));
                    sscanf_s(line, " hideemitters %s", hideEmitters, (int32)sizeof(hideEmitters));
                    sscanf_s(line, " enablebackground %s", enableBackground, (int32)sizeof(enableBackground));
                    sscanf_s(line, " transparentbackground %s", transparentBackground, (int32)sizeof(transparentBackground));
                    sscanf_s(line, " backgroundcolor %f %f %f", &renderOptions.backgroundCol.x, &renderOptions.backgroundCol.y, &renderOptions.backgroundCol.z);
                    sscanf_s(line, " independentrendersize %s", independentRenderSize, (int32)sizeof(independentRenderSize));
                    sscanf_s(line, " envmaprotation %f", &renderOptions.envMapRot);
                    sscanf_s(line, " enableroughnessmollification %s", enableRoughnessMollification, (int32)sizeof(enableRoughnessMollification));
                    sscanf_s(line, " roughnessmollificationamt %f", &renderOptions.roughnessMollificationAmt);
                    sscanf_s(line, " enablevolumemis %s", enableVolumeMIS, (int32)sizeof(enableVolumeMIS));
                    sscanf_s(line, " enableuniformlight %s", enableUniformLight, (int32)sizeof(enableUniformLight));
                    sscanf_s(line, " uniformlightcolor %f %f %f", &renderOptions.uniformLightCol.x, &renderOptions.uniformLightCol.y, &renderOptions.uniformLightCol.z);
                }

                if (strcmp(envMap, "none") != 0)
                {
                    scene->AddEnvMap(path + envMap);
                    renderOptions.enableEnvMap = true;
                }
                else
                    renderOptions.enableEnvMap = false;

                if (strcmp(enableAces, "false") == 0)
                    renderOptions.enableAces = false;
                else if (strcmp(enableAces, "true") == 0)
                    renderOptions.enableAces = true;

                if (strcmp(enableRR, "false") == 0)
                    renderOptions.enableRR = false;
                else if (strcmp(enableRR, "true") == 0)
                    renderOptions.enableRR = true;

                if (strcmp(openglNormalMap, "false") == 0)
                    renderOptions.openglNormalMap = false;
                else if (strcmp(openglNormalMap, "true") == 0)
                    renderOptions.openglNormalMap = true;

                if (strcmp(hideEmitters, "false") == 0)
                    renderOptions.hideEmitters = false;
                else if (strcmp(hideEmitters, "true") == 0)
                    renderOptions.hideEmitters = true;

                if (strcmp(enableBackground, "false") == 0)
                    renderOptions.enableBackground = false;
                else if (strcmp(enableBackground, "true") == 0)
                    renderOptions.enableBackground = true;

                if (strcmp(transparentBackground, "false") == 0)
                    renderOptions.transparentBackground = false;
                else if (strcmp(transparentBackground, "true") == 0)
                    renderOptions.transparentBackground = true;

                if (strcmp(independentRenderSize, "false") == 0)
                    renderOptions.independentRenderSize = false;
                else if (strcmp(independentRenderSize, "true") == 0)
                    renderOptions.independentRenderSize = true;

                if (strcmp(enableTonemap, "false") == 0)
                    renderOptions.enableTonemap = false;
                else if (strcmp(enableTonemap, "true") == 0)
                    renderOptions.enableTonemap = true;

                if (strcmp(enableRoughnessMollification, "false") == 0)
                    renderOptions.enableRoughnessMollification = false;
                else if (strcmp(enableRoughnessMollification, "true") == 0)
                    renderOptions.enableRoughnessMollification = true;

                if (strcmp(enableVolumeMIS, "false") == 0)
                    renderOptions.enableVolumeMIS = false;
                else if (strcmp(enableVolumeMIS, "true") == 0)
                    renderOptions.enableVolumeMIS = true;

                if (strcmp(enableUniformLight, "false") == 0)
                    renderOptions.enableUniformLight = false;
                else if (strcmp(enableUniformLight, "true") == 0)
                    renderOptions.enableUniformLight = true;

                if (!renderOptions.independentRenderSize)
                    renderOptions.windowResolution = renderOptions.renderResolution;
            }


            //--------------------------------------------
            // Mesh

            if (strstr(line, "mesh"))
            {
                std::string filename;
                Vector4 rotQuat;        // todo : need to add jQuat
                Matrix xform, translate, rot, scale;
                int material_id = 0; // Default Material ID
                char meshName[200] = "none";
                bool matrixProvided = false;

                while (fgets(line, kMaxLineLength, file))
                {
                    // end group
                    if (strchr(line, '}'))
                        break;

                    char file[2048];
                    char matName[100];

                    sscanf_s(line, " name %[^\t\n]s", meshName, (int32)sizeof(meshName));

                    if (sscanf_s(line, " file %s", file, (int32)sizeof(file)) == 1)
                        filename = path + file;

                    if (sscanf_s(line, " material %s", matName, (int32)sizeof(matName)) == 1)
                    {
                        // look up material in dictionary
                        if (materialMap.find(matName) != materialMap.end())
                            material_id = materialMap[matName].id;
                        else
                            printf("Could not find material %s\n", matName);
                    }

                    if (sscanf_s(line, " matrix %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
                        &xform.m[0][0], &xform.m[1][0], &xform.m[2][0], &xform.m[3][0],
                        &xform.m[0][1], &xform.m[1][1], &xform.m[2][1], &xform.m[3][1],
                        &xform.m[0][2], &xform.m[1][2], &xform.m[2][2], &xform.m[3][2],
                        &xform.m[0][3], &xform.m[1][3], &xform.m[2][3], &xform.m[3][3]
                    ) != 0)
                        matrixProvided = true;

                    sscanf_s(line, " position %f %f %f", &translate.m[3][0], &translate.m[3][1], &translate.m[3][2]);
                    sscanf_s(line, " scale %f %f %f", &scale.m[0][0], &scale.m[1][1], &scale.m[2][2]);
                    if (sscanf_s(line, " rotation %f %f %f %f", &rotQuat.x, &rotQuat.y, &rotQuat.z, &rotQuat.w) != 0)
                    {
                        // todo
                        //rot = Matrix::QuatToMatrix(rotQuat.x, rotQuat.y, rotQuat.z, rotQuat.w);
                    }
                }

                if (!filename.empty())
                {
                    int mesh_id = scene->AddMesh(filename);
                    if (mesh_id != -1)
                    {
                        std::string instanceName;

                        if (strcmp(meshName, "none") != 0)
                            instanceName = std::string(meshName);
                        else
                        {
                            std::size_t pos = filename.find_last_of("/\\");
                            instanceName = filename.substr(pos + 1);
                        }

                        Matrix transformMat;

                        if (matrixProvided)
                            transformMat = xform;
                        else
                            transformMat = scale * rot * translate;

                        MeshInstance instance(instanceName, mesh_id, transformMat, material_id);
                        scene->AddMeshInstance(instance);
                    }
                }
            }

            //--------------------------------------------
            // GLTF

            if (strstr(line, "gltf"))
            {
                std::string filename;
                Vector4 rotQuat;
                Matrix xform, translate, rot, scale;
                bool matrixProvided = false;

                while (fgets(line, kMaxLineLength, file))
                {
                    // end group
                    if (strchr(line, '}'))
                        break;

                    char file[2048];

                    if (sscanf_s(line, " file %s", file, (int32)sizeof(file)) == 1)
                        filename = path + file;

                    if (sscanf_s(line, " matrix %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
                        &xform.m[0][0], &xform.m[1][0], &xform.m[2][0], &xform.m[3][0],
                        &xform.m[0][1], &xform.m[1][1], &xform.m[2][1], &xform.m[3][1],
                        &xform.m[0][2], &xform.m[1][2], &xform.m[2][2], &xform.m[3][2],
                        &xform.m[0][3], &xform.m[1][3], &xform.m[2][3], &xform.m[3][3]
                    ) != 0)
                        matrixProvided = true;

                    sscanf_s(line, " position %f %f %f", &translate.m[3][0], &translate.m[3][1], &translate.m[3][2]);
                    sscanf_s(line, " scale %f %f %f", &scale.m[0][0], &scale.m[1][1], &scale.m[2][2]);
                    if (sscanf_s(line, " rotation %f %f %f %f", &rotQuat.x, &rotQuat.y, &rotQuat.z, &rotQuat.w) != 0)
                    {
                        // Todo
                        //rot = Mat4::QuatToMatrix(rotQuat.x, rotQuat.y, rotQuat.z, rotQuat.w);
                    }
                }

                if (!filename.empty())
                {
                    std::string ext = filename.substr(filename.find_last_of(".") + 1);

                    bool success = false;
                    Matrix transformMat;

                    if (matrixProvided)
                        transformMat = xform;
                    else
                        transformMat = scale * rot * translate;

                    // TODO: Add support for instancing.
                    // If the same gltf is loaded multiple times then mesh data gets duplicated
                    if (ext == "gltf")
                        success = LoadGLTF(filename, scene, renderOptions, transformMat, false);
                    else if (ext == "glb")
                        success = LoadGLTF(filename, scene, renderOptions, transformMat, true);

                    if (!success)
                    {
                        printf("Unable to load gltf %s\n", filename.c_str());
                        exit(0);
                    }
                }
            }
        }

        fclose(file);

        return true;
    }
}
