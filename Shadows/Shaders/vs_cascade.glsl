#version 330 core

precision mediump float;

layout(location = 0) in vec3 Pos;

uniform mat4 MVP;
uniform mat4 lightMVP[3];

out vec4 lightPos[3];
out float ClipSpaceZ;

void main()
{
	for(int i=0;i<3;++i)
	{
		lightPos[i] = lightMVP[i] * vec4(Pos, 1.0);
	}
    gl_Position = MVP * vec4(Pos, 1.0);
	ClipSpaceZ = gl_Position.z;
}