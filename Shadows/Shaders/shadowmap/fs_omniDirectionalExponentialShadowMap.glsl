#version 330 core

#include "common.glsl"

precision mediump float;

#define MAX_NUM_OF_POINT_LIGHT 3
#define MAX_NUM_OF_SPOT_LIGHT 3

uniform int NumOfPointLight;
uniform int NumOfSpotLight;

uniform jPointLight PointLight[MAX_NUM_OF_POINT_LIGHT];
uniform jSpotLight SpotLight[MAX_NUM_OF_SPOT_LIGHT];

uniform float PointLightZNear;
uniform float PointLightZFar;
uniform float SpotLightZNear;
uniform float SpotLightZFar;

uniform float PointLightESM_C;
uniform float SpotLightESM_C;

in vec3 Pos_;

void main()
{
    if (NumOfPointLight > 0)
    {
        vec3 lightDir = Pos_ - PointLight[0].LightPos;
        float distSquared = dot(lightDir.xyz, lightDir.xyz);
        float distFromLight = (sqrt(distSquared) - PointLightZNear) / (PointLightZFar - PointLightZNear);
        
        gl_FragData[0].x = exp(distFromLight * PointLightESM_C);
        gl_FragData[0].w = 1.0;
    }
    else if (NumOfSpotLight > 0)
    {
        vec3 lightDir = Pos_ - SpotLight[0].LightPos;
        float distSquared = dot(lightDir.xyz, lightDir.xyz);
        float distFromLight = (sqrt(distSquared) - SpotLightZNear) / (SpotLightZFar - SpotLightZNear);
        
        gl_FragData[0].x = exp(distFromLight * SpotLightESM_C);
        gl_FragData[0].w = 1.0;
    }
}