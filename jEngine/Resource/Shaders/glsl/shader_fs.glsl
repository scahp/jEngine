#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragShadowPosition;

//layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform DirectionalLightUniformBuffer
{
	vec3 Direction;
	float SpecularPow;

	vec3 Color;
	float padding0;

	vec3 DiffuseIntensity;
	float padding1;

	vec3 SpecularIntensity;
	float padding2;

	mat4 ShadowVP;
	mat4 ShadowV;
		
	vec3 LightPos;
	float padding3;

	vec2 ShadowMapSize;
	float Near;
	float Far;
} DirectionalLight;

layout(set = 1, binding = 0) uniform RenderObjectUniformBuffer
{
	mat4 M;
	mat4 MV;
	mat4 MVP;
	mat4 InvM;
} RenderObjectParam;

layout(set = 0, binding = 1) uniform sampler2D DirectionalLightShadowMap;

void main()
{
    float lit = 1.0;
	if (-1.0 <= fragShadowPosition.z && fragShadowPosition.z <= 1.0)
	{
		float shadowMapDist = texture(DirectionalLightShadowMap, fragShadowPosition.xy * 0.5 + 0.5).r;
		if (fragShadowPosition.z > shadowMapDist + 0.001)
		{
			lit = 0.5;
		}
	}

	float Intensity = dot(fragNormal, -DirectionalLight.Direction) * lit;
	outColor = vec4(fragColor.xyz * Intensity, 1.0);
}
