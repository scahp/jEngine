#version 430 core

#include "common.glsl"

precision mediump float;

#define MAX_NUM_OF_POINT_LIGHT 1
#define MAX_NUM_OF_SPOT_LIGHT 1

uniform int NumOfPointLight;
uniform int NumOfSpotLight;

layout (std140) uniform PointLightBlock
{
	jPointLight PointLight[MAX_NUM_OF_POINT_LIGHT];
};

layout (std140) uniform SpotLightBlock
{
	jSpotLight SpotLight[MAX_NUM_OF_SPOT_LIGHT];
};

in vec4 fragPos_;
out vec4 color;

void main()
{
    if (NumOfPointLight > 0)
    {
        vec3 lightDir = fragPos_.xyz - PointLight[0].LightPos;

        float dist = dot(lightDir.xyz, lightDir.xyz);
		color.x = dist;
		color.y = sqrt(dist);
		color.w = 1.0;
    }
    else if (NumOfSpotLight > 0)
    {
        vec3 lightDir = fragPos_.xyz - SpotLight[0].LightPos;

        float dist = dot(lightDir.xyz, lightDir.xyz);
        color.x = dist;
        color.y = sqrt(dist);
        color.w = 1.0;
    }
}