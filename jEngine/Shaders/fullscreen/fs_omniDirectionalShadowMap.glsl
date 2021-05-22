#include "common.glsl"

precision mediump float;

#define MAX_NUM_OF_POINT_LIGHT 3
#define MAX_NUM_OF_SPOT_LIGHT 3

uniform int NumOfPointLight;
uniform int NumOfSpotLight;

uniform jPointLight PointLight[MAX_NUM_OF_POINT_LIGHT];
uniform jSpotLight SpotLight[MAX_NUM_OF_SPOT_LIGHT];

varying vec3 Pos_;

void main()
{
    if (NumOfPointLight > 0)
    {
        vec3 lightDir = Pos_ - PointLight[0].LightPos;

        float dist = dot(lightDir.xyz, lightDir.xyz);
        gl_FragData[0].x = dist;
        gl_FragData[0].y = sqrt(dist);
        gl_FragData[0].w = 1.0;
    }
    else if (NumOfSpotLight > 0)
    {
        vec3 lightDir = Pos_ - SpotLight[0].LightPos;

        float dist = dot(lightDir.xyz, lightDir.xyz);
        gl_FragData[0].x = dist;
        gl_FragData[0].y = sqrt(dist);
        gl_FragData[0].w = 1.0;
    }
}