precision mediump float;

struct jAmbientLight
{
    vec3 Color;
    vec3 Intensity;
};

struct jDirectionalLight
{
    vec3 LightDirection;
    vec3 Color;
    vec3 DiffuseLightIntensity;
    vec3 SpecularLightIntensity;
    float SpecularPow;
};

struct jPointLight
{
    vec3 LightPos;
    vec3 Color;
    vec3 DiffuseLightIntensity;
    vec3 SpecularLightIntensity;
    float SpecularPow;
    float MaxDistance;
};

struct jSpotLight
{
    vec3 LightPos;
    vec3 Direction;
    vec3 Color;
    vec3 DiffuseLightIntensity;
    vec3 SpecularLightIntensity;
    float SpecularPow;
    float MaxDistance;
    float PenumbraRadian;
    float UmbraRadian;
};

struct jMaterial
{
    vec3 Diffuse;
    vec3 Emissive;
    vec3 Specular;
    float Shininess;
};

vec3 TransformPos(mat4 m, vec3 v)
{
    return (m * vec4(v, 1.0)).xyz;
}

vec3 TransformNormal(mat4 m, vec3 v)
{
    return (m * vec4(v, 0.0)).xyz;
}

float WindowingFunction(float value, float maxValue)
{
    return pow(max(0.0, 1.0 - pow(value / maxValue, 4.0)), 2.0);
}

float DistanceAttenuation(float distance, float maxDistance)
{
    const float refDistance = 50.0;
    float attenuation = (refDistance * refDistance) / ((distance * distance) + 1.0);
    return attenuation * WindowingFunction(distance, maxDistance);
}

float DiretionalFalloff(float lightRadian, float penumbraRadian, float umbraRadian)
{
    float t = clamp((cos(lightRadian) - cos(umbraRadian)) / (cos(penumbraRadian) - cos(umbraRadian)), 0.0, 1.0);
    return t * t;
}

vec3 GetAmbientLight(jAmbientLight light)
{
    return light.Color * light.Intensity;
}

vec3 GetDirectionalLightDiffuse(jDirectionalLight light, vec3 normal)
{
    return light.Color * max(dot(-light.LightDirection, normal), 0.0) * light.DiffuseLightIntensity;
}

vec3 GetDirectionalLightSpecular(jDirectionalLight light, vec3 reflectLightDir, vec3 viewDir)
{
    return light.Color * pow(max(dot(reflectLightDir, viewDir), 0.0), light.SpecularPow) * light.SpecularLightIntensity;
}

vec3 GetDirectionalLight(jDirectionalLight light, vec3 normal, vec3 viewDir)
{
    vec3 lightDir = normalize(-light.LightDirection);
    vec3 reflectLightDir = 2.0 * max(dot(lightDir, normal), 0.0) * normal - lightDir;
    return (GetDirectionalLightDiffuse(light, normal) + GetDirectionalLightSpecular(light, reflectLightDir, viewDir));
}

vec3 GetPointLightDiffuse(jPointLight light, vec3 normal, vec3 lightDir)
{
    return light.Color * max(dot(lightDir, normal), 0.0) * light.DiffuseLightIntensity;
}

vec3 GetPointLightSpecular(jPointLight light, vec3 reflectLightDir, vec3 viewDir)
{
    return light.Color * pow(max(dot(reflectLightDir, viewDir), 0.0), light.SpecularPow) * light.SpecularLightIntensity;
}

vec3 GetPointLight(jPointLight light, vec3 normal, vec3 pixelPos, vec3 viewDir)
{
    vec3 lightDir = light.LightPos - pixelPos;
    float pointLightDistance = length(lightDir);
    lightDir = normalize(lightDir);
    vec3 reflectLightDir = 2.0 * max(dot(lightDir, normal), 0.0) * normal - lightDir;
   
    return (GetPointLightDiffuse(light, normal, lightDir) + GetPointLightSpecular(light, reflectLightDir, viewDir)) * DistanceAttenuation(pointLightDistance, light.MaxDistance);
}

vec3 GetSpotLightDiffuse(jSpotLight light, vec3 normal, vec3 lightDir)
{
    return light.Color * max(dot(lightDir, normal), 0.0) * light.DiffuseLightIntensity;
}

vec3 GetSpotLightSpecular(jSpotLight light, vec3 reflectLightDir, vec3 viewDir)
{
    return light.Color * pow(max(dot(reflectLightDir, viewDir), 0.0), light.SpecularPow) * light.SpecularLightIntensity;
}

vec3 GetSpotLight(jSpotLight light, vec3 normal, vec3 pixelPos, vec3 viewDir)
{
    vec3 lightDir = light.LightPos - pixelPos;
    float distance = length(lightDir);
    lightDir = normalize(lightDir);
    vec3 reflectLightDir = 2.0 * max(dot(lightDir, normal), 0.0) * normal - lightDir;

    float lightRadian = acos(dot(lightDir, -light.Direction));

    return (GetSpotLightDiffuse(light, normal, lightDir)
     + GetSpotLightSpecular(light, reflectLightDir, viewDir))
      * DistanceAttenuation(distance, light.MaxDistance)
      * DiretionalFalloff(lightRadian, light.PenumbraRadian, light.UmbraRadian);
}

vec4 EncodeFloat(float depth)
{
    const vec4 bitShift = vec4(256 * 256 * 256, 256 * 256, 256, 1.0);
    const vec4 bitMask = vec4(0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
    vec4 comp = fract(depth * bitShift);
    comp -= comp.xxyz * bitMask;
    return comp;
}

float DecodeFloat(vec4 color)
{
    const vec4 bitShift = vec4(1.0 / (256.0 * 256.0 * 256.0), 1.0 / (256.0 * 256.0), 1.0 / 256.0, 1.0);
    return dot(color, bitShift);
}

struct TexArrayUV
{
  int index;
  float u;
  float v;
};

TexArrayUV convert_xyz_to_texarray_uv(vec3 direction)
{
    TexArrayUV result;

    float x = direction.x;
    float y = direction.y;
    float z = direction.z;

    float absX = abs(x);
    float absY = abs(y);
    float absZ = abs(z);

    bool isXPositive = x > 0.0 ? true : false;
    bool isYPositive = y > 0.0 ? true : false;
    bool isZPositive = z > 0.0 ? true : false;

    float maxAxis;
    float uc;
    float vc;

    if (absX >= absY && absX >= absZ)
    {
        // POSITIVE X
        if (isXPositive) 
        {
            // u (0 to 1) goes from +z to -z
            // v (0 to 1) goes from -y to +y
            maxAxis = absX;
            uc = -z;
            vc = y;
            result.index = 0;
        }
        else    // NEGATIVE X
        {
            // u (0 to 1) goes from -z to +z
            // v (0 to 1) goes from -y to +y
            maxAxis = absX;
            uc = z;
            vc = y;
            result.index = 1;
        }
    }

    if (absY >= absX && absY >= absZ)
    {
        // POSITIVE Y
        if (isYPositive) 
        {
            // u (0 to 1) goes from -x to +x
            // v (0 to 1) goes from +z to -z
            maxAxis = absY;
            uc = x;
            vc = -z;
            result.index = 2;
        }
        else // NEGATIVE Y
        {
            // u (0 to 1) goes from -x to +x
            // v (0 to 1) goes from -z to +z
            maxAxis = absY;
            uc = x;
            vc = z;
            result.index = 3;
        }
    }

    if (absZ >= absX && absZ >= absY)
    {
        // POSITIVE Z
        if (isZPositive) 
        {
            // u (0 to 1) goes from -x to +x
            // v (0 to 1) goes from -y to +y
            maxAxis = absZ;
            uc = x;
            vc = y;
            result.index = 4;
        }
        else // NEGATIVE Z
        {
            // u (0 to 1) goes from +x to -x
            // v (0 to 1) goes from -y to +y
            maxAxis = absZ;
            uc = -x;
            vc = y;
            result.index = 5;
        }
    }

    // Convert range from -1 to 1 to 0 to 1
    result.u = 1.0-(0.5 * (uc / maxAxis + 1.0));
    result.v = 0.5 * (vc / maxAxis + 1.0);
    return result;
}

TexArrayUV MakeTexArrayUV(TexArrayUV uv)
{
    //for(int i=0;i<2;++i)        // to cover uv is out of range simultaneously. loop over 2 times.
    {
        if (uv.index == 0)            // Positive X
        {
            if (uv.u > 1.0)
            {
                uv.u = 1.0 - uv.u;
                uv.index = 4;
            }
            else if (uv.u < 0.0)
            {
                uv.u = 1.0 + uv.u;
                uv.index = 5;
            }            
            else if (uv.v > 1.0)
            {
                float t = uv.u;
                uv.u = uv.v - 1.0;
                uv.v = 1.0 - t;
                uv.index = 2;
            }
            else if (uv.v < 0.0)
            {
                float t = uv.u;
                uv.u = -uv.v;
                uv.v = t;
                uv.index = 3;
            }
        }
        else if (uv.index == 1)       // Negative X
        {
            if (uv.u > 1.0)
            {
                uv.u = 1.0 - uv.u;
                uv.index = 5;
            }
            else if (uv.u < 0.0)
            {
                uv.u = 1.0 + uv.u;
                uv.index = 4;
            }            
            else if (uv.v > 1.0)
            {
                float t = uv.u;
                uv.u = 2.0 - uv.v;
                uv.v = t;
                uv.index = 2;
            }
            else if (uv.v < 0.0)
            {
                float t = uv.u;
                uv.u = 1.0 + uv.v;
                uv.v = 1.0 - t;
                uv.index = 3;
            }
        }
        else if (uv.index == 2)       // Positive Y
        {
            if (uv.u > 1.0)
            {
                float t = uv.u;
                uv.u = uv.v;
                uv.v = 2.0 - t;
                uv.index = 1;
            }
            else if (uv.u < 0.0)
            {
                float t = uv.u;
                uv.u = 1.0 - uv.v;
                uv.v = 1.0 + t;
                uv.index = 0;
            }            
            else if (uv.v > 1.0)
            {
                uv.u = 1.0 - uv.u;
                uv.v = 2.0 - uv.v;
                uv.index = 5;
            }
            else if (uv.v < 0.0)
            {
                uv.v = 1.0 + uv.v;
                uv.index = 4;
            }
        }
        else if (uv.index == 3)       // Negative Y
        {
            if (uv.u > 1.0)
            {
                float t = uv.u;
                uv.u = 1.0 - uv.v;
                uv.v = 1.0 - t;
                uv.index = 1;
            }
            else if (uv.u < 0.0)
            {
                float t = uv.u;
                uv.u = uv.v;
                uv.v = -t;
                uv.index = 0;
            }            
            else if (uv.v > 1.0)
            {
                uv.v = 1.0 - uv.v;
                uv.index = 4;
            }
            else if (uv.v < 0.0)
            {
                uv.u = 1.0-uv.u;
                uv.v = -uv.v;
                uv.index = 5;
            }
        }
        else if (uv.index == 4)       // Positive Z
        {
            if (uv.u > 1.0)
            {
                uv.u = 1.0 - uv.u;
                uv.index = 1;
            }
            else if (uv.u < 0.0)
            {
                uv.u = 1.0 + uv.u;
                uv.index = 0;
            }            
            else if (uv.v > 1.0)
            {
                uv.v = uv.v - 1.0;
                uv.index = 2;
            }
            else if (uv.v < 0.0)
            {
                uv.v = 1.0 + uv.v;
                uv.index = 3;
            }
        }
        else if (uv.index == 5)       // Negative Z
        {
            if (uv.u > 1.0)
            {
                uv.u = 1.0 - uv.u;
                uv.index = 0;
            }
            else if (uv.u < 0.0)
            {
                uv.u = 1.0 + uv.u;
                uv.index = 1;
            }            
            else if (uv.v > 1.0)
            {
                uv.u = 1.0 - uv.u;
                uv.v = 2.0 - uv.v;
                uv.index = 2;
            }
            else if (uv.v < 0.0)
            {
                uv.u = 1.0 - uv.u;
                uv.v = -uv.v;
                uv.index = 3;
            }
        }
    }
    return uv;
}