#version 430 core

layout (local_size_x = 32, local_size_y = 32) in;

layout (binding=0, rgba8) readonly uniform image2D img_input;
layout (binding=1, rgba8) writeonly uniform image2D img_output;

layout (std430, binding=2) buffer shader_data
{ 
  vec4 camera_position;
  vec4 light_position;
  vec4 light_diffuse;
};

layout (std430) buffer StartElementBufEntry
{
	uint start[];
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

layout (binding = 3, offset = 0) uniform atomic_uint LinkedListCounter;

void AddNewLinkedList()
{
	uint counter = atomicCounterIncrement(LinkedListCounter);
	LinkedListData[counter].depth = float(counter);

	uint originalValue = atomicExchange(start[0], counter);
	LinkedListData[counter].next = int(originalValue);
}

void main(void)
{
	vec4 texel;
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);

	//if (p.x == 1 && p.y == 1)
	AddNewLinkedList();

	camera_position = vec4(100, 200, 300, 400);
	light_position = vec4(101, 201, 301, 401);
	light_diffuse = vec4(102, 202, 302, 402);

	texel = imageLoad(img_input, p);
	texel = vec4(1.0) - texel;
	imageStore(img_output, p, texel);
}
