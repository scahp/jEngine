#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject
{
    mat4 Model;
    mat4 View;
    mat4 Proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

/*
// dvec3 같은 64비트 vectors 은 location을 하나 더 사용할 수 있음.
layout(location = 0) in dvec3 inPosition;   // dvec3 is 64 bit vector.
layout(location = 2) in vec3 inColor;
*/

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() 
{
    gl_Position = ubo.Proj * ubo.View * ubo.Model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
