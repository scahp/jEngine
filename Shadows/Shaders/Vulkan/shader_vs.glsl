#version 450
#extension GL_ARB_separate_shader_objects : enable

//layout(set = 0,binding = 0) uniform UniformBufferObject
//{
//    mat4 Model;
//    mat4 View;
//    mat4 Proj;
//} ubo;

layout(set = 0, binding = 0) uniform RenderObjectUniformBuffer
{
	mat4 M;
    mat4 V;
	mat4 P;
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

void main() 
{
    gl_Position = RenderObjectParam.P * RenderObjectParam.V * RenderObjectParam.M * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
