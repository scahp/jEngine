#version 330 core

precision mediump float;

out vec4 color;

#define NUM_CASCADES 3
in vec4 LightSpacePos[NUM_CASCADES];
in vec4 Pos_;

float CalcShadowFactor(int CascadeIndex)
{
	vec3 ProjCoords = LightSpacePos[CascadeIndex].xyz / LightSpacePos[CascadeIndex].w;
	ProjCoords = 0.5 * ProjCoords + 0.5;
	vec3 pos = Pos_.xyz / Pos_.w;
	pos = 0.5 * pos + 0.5;

	if (ProjCoords.z < pos.z)
		return 1.0;

	return 0.0;
	

	//vec2 UVCoords;
	//UVCoords.x = 0.5 * ProjCoords.x + 0.5;
	//UVCoords.y = 0.5 * ProjCoords.y + 0.5;



	//float z = 0.5 * ProjCoords.z + 0.5;
	//float Depth = texture(gShadowMap[CascadeIndex], UVCoords).x;

	//if (Depth < z + 0.00001)
	//	return 0.5;
	//else
	//	return 1.0;
}

void main()
{
	int index = -1;
	for (int i = 0; i < NUM_CASCADES; ++i)
	{
		if (CalcShadowFactor(i) > 0.0)
		{
			index = i;
			break;
		}
	}

	vec3 pos = Pos_.xyz / Pos_.w;

	vec3 ProjCoords = LightSpacePos[0].xyz / LightSpacePos[0].w;
	vec3 ProjCoords2 = LightSpacePos[1].xyz / LightSpacePos[1].w;
	vec3 ProjCoords3 = LightSpacePos[2].xyz / LightSpacePos[2].w;
	//ProjCoords = ProjCoords * 0.5 + 0.5;

	//if (-1.0 == index)
	//	color = vec4(1.0, 0.0, pos.z, 1.0);
	//else if (0 == index)
	//if (ProjCoords.z < -0.01)
		color = vec4(Pos_.x, Pos_.y, Pos_.z, 1.0);
	//else
	//	color = vec4(0.0, 0.0, 0.0, 1.0);
	//else if (1 == index)
	//	color = vec4(0.0, 1.0, pos.z, 1.0);
	//else if (2 == index)
	//	color = vec4(1.0, 0.0, pos.z, 1.0);
}