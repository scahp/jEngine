#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;

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

layout(set = 0, binding = 1) uniform sampler2D DirectionalLightShadowMap;

void main() 
{
    vec4 shadow = vec4(texture(DirectionalLightShadowMap, fragTexCoord).rgb, 1.0);

	float Intensity = dot(fragNormal, -DirectionalLight.Direction);
	outColor = vec4(fragColor.xyz * Intensity, 1.0);
	outColor.z += shadow.x * 0.01;
    //outColor = vec4(fragColor);
}
