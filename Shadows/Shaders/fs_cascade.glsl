#version 330 core

precision mediump float;

out vec4 color;

in vec4 lightPos[3];
in float ClipSpaceZ;

uniform float cascadeEnd[4];

void main()
{
	vec4 result[3];
	for(int i=0;i<3;++i)
	{
		result[i] = (lightPos[i] / lightPos[i].w) * 0.5 + 0.5;
	}

	int cascadeIndex = 0;
	for(int i=0;i<4;++i)
	{
		if (ClipSpaceZ < cascadeEnd[i])
		{
			cascadeIndex = i;
			break;
		}
	}

	if (cascadeIndex == 0)
	{
		color = vec4(1.0, 0.0, 0.0, 1.0);
	}
	else if (cascadeIndex == 1)
	{
		color = vec4(0.0, 1.0, 0.0, 1.0);
	}
	else if (cascadeIndex == 2)
	{
		color = vec4(0.0, 0.0, 1.0, 1.0);
	}
	else if (cascadeIndex == 3)
	{
		color = vec4(1.0, 1.0, 0.0, 1.0);
	}
	else
	{
		color = vec4(result[0].z, result[1].z, result[2].z, ClipSpaceZ);
	}

	//color = vec4(result[0].z, result[1].z, result[2].z, ClipSpaceZ);
}