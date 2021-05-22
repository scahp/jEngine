#version 330 core

precision highp float;

out vec4 color;

#define NUM_CASCADES 3
in vec4 LightSpacePos[NUM_CASCADES];
in vec4 Pos_;
in vec4 Color_;

uniform float cascadeEndsW[NUM_CASCADES];
uniform sampler2D cascade_shadow_sampler[NUM_CASCADES];

#define SHADOW_BIAS_DIRECTIONAL 0.01

bool IsInRange01(vec3 pos)
{
	return (pos.x >= 0.0 && pos.x <= 1.0 && pos.y >= 0.0 && pos.y <= 1.0 && pos.z >= 0.0 && pos.z <= 1.0);
}

int GetCascadeIndex(float viewSpaceZ)
{
	for (int i = 0; i < NUM_CASCADES; ++i)
	{
		if (-viewSpaceZ < cascadeEndsW[i])
			return i;
	}
	return -1;
}

void main()
{
	vec4 lightClipSpacePos[NUM_CASCADES];
	for (int i = 0; i < NUM_CASCADES; ++i)
		lightClipSpacePos[i] = (LightSpacePos[i] / LightSpacePos[i].w) * 0.5 + vec4(0.5);

	//float z = (Pos_.z / Pos_.w) * 0.5 + 0.5;

	color = vec4(0.0, 0.0, 0.0, 1.0);

	//int tempIndex = GetCascadeIndex(Pos_.z);
	//switch (tempIndex)
	//{
	//case 0:
	//	color = vec4(1.0, 0.0, 0.0, 0.0);
	//	break;
	//case 1:
	//	color = vec4(0.0, 1.0, 0.0, 0.0);
	//	break;
	//case 2:
	//	color = vec4(0.0, 0.0, 1.0, 0.0);
	//	break;
	//}

	//if (IsInRange01(lightClipSpacePos[2].xyz))
	//{
	//	color += vec4(1.0, 0.0, 0.0, 0.0);
	//}
	//else if (IsInRange01(lightClipSpacePos[1].xyz))
	//{
	//	color += vec4(0.0, 0.0, 1.0, 0.0);
	//}
	//else if (IsInRange01(lightClipSpacePos[0].xyz))
	//{
	//	color += vec4(0.0, 1.0, 0.0, 0.0);
	//}

	//float z = texture(cascade_shadow_sampler[0], lightClipSpacePos[0].xy).x;
	//color = vec4(gl_FragCoord.z, gl_FragCoord.z, gl_FragCoord.z, 1.0);
	//return;

	//for (int i = NUM_CASCADES - 1; i >= 0; --i)

	for (int i = 0; i < NUM_CASCADES; ++i)
	{
		if (!IsInRange01(lightClipSpacePos[i].xyz))
			continue;

		if (lightClipSpacePos[i].z >= texture(cascade_shadow_sampler[i], lightClipSpacePos[i].xy).x + 0.01)
		{
			return;
		}
	}

	color = Color_;

	return;
}