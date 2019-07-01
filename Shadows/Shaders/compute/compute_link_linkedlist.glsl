#version 430 core

layout (local_size_x = 16, local_size_y = 8) in;

#define DS_LINKED_LIST_DEPTH 50

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

struct NeighborData
{
	int right;
	int top;
};

layout (std430) buffer LinkedListEntryNeighbors
{
	NeighborData NeighborListData[];
};

struct LocalListEntry
{
	float depth;
	float alpha;
};

uniform int ShadowMapWidth;
uniform int ShadowMapHeight;

void main(void)
{
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);

	int centerStartIndex = int(start[uv.y * ShadowMapWidth + uv.x]);
	if (-1 == centerStartIndex)
		return;

	int rightStartIndex;
	int topStartIndex;

	if (uv.x != ShadowMapWidth - 1)
		rightStartIndex = int(start[uv.y * ShadowMapWidth + uv.x + 1]);
	else
		rightStartIndex = -1;

	if (uv.y != ShadowMapHeight - 1)
		topStartIndex = int(start[(uv.y + 1) * ShadowMapWidth + uv.x]);
	else
		topStartIndex = -1;

	NodeData currentEntryRight;
	NodeData currentEntryTop;
	if (rightStartIndex != -1)
		currentEntryRight = LinkedListData[rightStartIndex];
	if (topStartIndex != -1)
		currentEntryTop = LinkedListData[topStartIndex];

	NodeData temp;
	float depth;
	for(int i=0;i<DS_LINKED_LIST_DEPTH;++i)
	{
		depth = LinkedListData[centerStartIndex].depth;
		
		for(int k = 0; k < DS_LINKED_LIST_DEPTH; ++k)
		{
			if(rightStartIndex == -1 || currentEntryRight.next == -1)
				break;
			temp = LinkedListData[currentEntryRight.next];
			if(depth > temp.depth)
				break;
			rightStartIndex = currentEntryRight.next;
			currentEntryRight = temp;
		}
		NeighborListData[centerStartIndex].right = rightStartIndex;
		
		for(int k = 0; k < DS_LINKED_LIST_DEPTH; ++k)
		{
			if(topStartIndex == -1 || currentEntryTop.next == -1)
				break;
			temp = LinkedListData[currentEntryTop.next];
			if(depth > temp.depth)
				break;
			topStartIndex = currentEntryTop.next;
			currentEntryTop = temp;
		}
		NeighborListData[centerStartIndex].top = topStartIndex;
		
		centerStartIndex = LinkedListData[centerStartIndex].next;
		if(centerStartIndex == -1)
			break;
	}
}
