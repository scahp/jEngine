#version 430 core

precision mediump float;

layout (std140) uniform DirectionalLightShadowMapBlock
{
	mat4 ShadowVP;
	mat4 ShadowV;
	vec3 LightPos;      // Directional Light Pos �ӽ�
	float LightZNear;
	float LightZFar;
};

layout (std430) buffer StartElementBufEntry
{
	int start[];
};

struct NodeData
{
	float depth;
	float alpha;
	int next;
	int prev;
};

layout (std430) buffer LinkedListEntryDepthAlphaNext
{
	NodeData LinkedListData[];
};

uniform float DeepShadowAlpha;
uniform int ShadowMapWidth;

layout (binding = 3, offset = 0) uniform atomic_uint LinkedListCounter;
//layout (binding = 4, offset = 0) uniform atomic_uint LinkedListCounterTester;

in vec3 Pos_;

out vec4 color;

#define DEEP_SHADOW_MAP_BIAS 0.0

void main()
{
	vec3 lightDir = Pos_ - LightPos;
    float distSquared = dot(lightDir.xyz, lightDir.xyz);
    //float distFromLight = clamp((sqrt(distSquared) - LightZNear) / (LightZFar - LightZNear), 0.0, 1.0);
	vec4 tempShadowPos = (ShadowVP * vec4(Pos_, 1.0));

	float distFromLight = (tempShadowPos.z) / LightZFar + DEEP_SHADOW_MAP_BIAS;

	uint counter = atomicCounterIncrement(LinkedListCounter);
	LinkedListData[counter].depth = distFromLight;
	LinkedListData[counter].alpha = DeepShadowAlpha;
	
	vec2 uv = gl_FragCoord.xy;

	int index = int(uint(uv.y) * ShadowMapWidth + uint(uv.x));
	
	uint originalVal = atomicExchange(start[index], int(counter));
	LinkedListData[counter].next = int(originalVal);

	color = vec4(1.0, 0.0, 0.0, 1.0);
}