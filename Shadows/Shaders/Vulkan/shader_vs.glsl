#version 450
#extension GL_ARB_separate_shader_objects : enable

//layout(set = 0,binding = 0) uniform UniformBufferObject
//{
//    mat4 Model;
//    mat4 View;
//    mat4 Proj;
//} ubo;

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
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec2 inTexCoord;

/*
// dvec3 같은 64비트 vectors 은 location을 하나 더 사용할 수 있음.
layout(location = 0) in dvec3 inPosition;   // dvec3 is 64 bit vector.
layout(location = 2) in vec3 inColor;
*/

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec4 fragShadowPosition;

void main() 
{
    gl_Position = RenderObjectParam.MVP * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;

	fragNormal = normalize(mat3(RenderObjectParam.M) * inNormal);
	
	fragShadowPosition = DirectionalLight.ShadowVP * RenderObjectParam.M * vec4(inPosition, 1.0);
	fragShadowPosition.y = 1.0 - fragShadowPosition.y;
	fragShadowPosition /= fragShadowPosition.w;

	// 왼손 좌표계로 Color
	gl_Position.y = 1.0 - gl_Position.y;
	fragTexCoord.y = 1.0 - inTexCoord.y;
}
