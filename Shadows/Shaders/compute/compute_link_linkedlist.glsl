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

	if (uv.y != 0)
		topStartIndex = int(start[(uv.y - 1) * ShadowMapWidth + uv.x]);
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

//	LocalListEntry tempList[DS_LINKED_LIST_DEPTH];
//	int curIndex = startIndex;
//	int numOfList = 0;
//	for(int i=0;i<DS_LINKED_LIST_DEPTH;++i)
//	{
//		tempList[i].depth = LinkedListData[curIndex].depth;
//		tempList[i].alpha = LinkedListData[curIndex].alpha;
//		++numOfList;
//		curIndex = LinkedListData[curIndex].next;
//		if (-1 == curIndex)
//			break;
//	}
//
//	int j = 0;
//	LocalListEntry temp;
//	for(int i=1;i<numOfList;++i)
//	{
//		temp = tempList[i];
//		for(j=i-1;(j >= 0) && (temp.depth > tempList[j].depth);--j)
//			tempList[j + 1] = tempList[j];
//		tempList[j + 1] = temp;
//	}
//
//	float shadingBefore = 1.0;
//	int prevIndex = startIndex;
//	curIndex = startIndex;
//	for(int i=0;i<numOfList;++i)
//	{
//		float shadingCurrent = shadingBefore * (1.0 - tempList[i].alpha);
//		LinkedListData[curIndex].depth = tempList[i].depth;
//		LinkedListData[curIndex].alpha = shadingCurrent;
//		if (shadingBefore - shadingCurrent <= 0.001)
//		{
//			LinkedListData[prevIndex].next = -1;
//			break;
//		}
//		shadingBefore = shadingCurrent;
//
//		prevIndex = curIndex;
//		curIndex = LinkedListData[curIndex].next;
//	}
//	if (curIndex != -1)
//		LinkedListData[curIndex].next = -1;
}
