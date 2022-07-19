#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform RenderObjectUniformBuffer
{
	mat4 M;
	mat4 MV;
	mat4 MVP;
	mat4 InvM;
} RenderObjectParam;

layout(location = 0) in vec3 inPosition;

void main() 
{
    gl_Position = RenderObjectParam.MVP * vec4(inPosition, 1.0);
	
	// ¿Þ¼Õ ÁÂÇ¥°è·Î Color
	gl_Position.y = 1.0 - gl_Position.y;
}
