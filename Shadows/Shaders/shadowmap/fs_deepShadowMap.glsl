#version 430 core

precision mediump float;

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

out vec4 color;

void main()
{
	uint counter = atomicCounterIncrement(LinkedListCounter);
	LinkedListData[counter].depth = gl_FragCoord.z + 0.00002;
	LinkedListData[counter].alpha = DeepShadowAlpha;
	
	vec2 uv = gl_FragCoord.xy;

	int index = int(uint(uv.y) * ShadowMapWidth + uint(uv.x));
	
	uint originalVal = atomicExchange(start[index], int(counter));
	LinkedListData[counter].next = int(originalVal);

	color = vec4(1.0, 0.0, 0.0, 1.0);
}