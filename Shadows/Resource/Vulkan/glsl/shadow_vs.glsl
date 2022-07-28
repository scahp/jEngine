#version 450
#extension GL_ARB_separate_shader_objects : enable

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

layout(location = 0) in vec3 inPosition;

void main() 
{
    gl_Position = DirectionalLight.ShadowVP * RenderObjectParam.M * vec4(inPosition, 1.0);
}
