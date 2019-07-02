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

	if(uv.y > (ShadowMapHeight - 1) || uv.x > (ShadowMapWidth - 1))
		return;

	int startIndex = int(start[uv.y * ShadowMapWidth + uv.x]);
	if (-1 == startIndex)
		return;

	LocalListEntry tempList[DS_LINKED_LIST_DEPTH];
	int curIndex = startIndex;
	int numOfList = 0;
	for(int i=0;i<DS_LINKED_LIST_DEPTH;++i)
	{
		tempList[i].depth = LinkedListData[curIndex].depth;
		tempList[i].alpha = LinkedListData[curIndex].alpha;
		++numOfList;
		curIndex = LinkedListData[curIndex].next;
		if (-1 == curIndex)
			break;
	}

	int j = 0;
	LocalListEntry temp;
	for(int i=1;i<numOfList;++i)
	{
		temp = tempList[i];
		for(j=i-1;(j >= 0) && (temp.depth < tempList[j].depth);--j)
			tempList[j + 1] = tempList[j];
		tempList[j + 1] = temp;
	}

	float shadingBefore = 1.0;
	int prevIndex = -1;
	curIndex = startIndex;
	for(int i=0;i<numOfList;++i)
	{
		float shadingCurrent = shadingBefore * (1.0 - tempList[i].alpha);
		if (shadingBefore - shadingCurrent <= 0.001)
		{
			if (prevIndex == -1)
				LinkedListData[prevIndex].next = -1;
			else
				LinkedListData[curIndex].next = -1;
			break;
		}
		LinkedListData[curIndex].depth = tempList[i].depth;
		LinkedListData[curIndex].alpha = shadingCurrent;
		LinkedListData[curIndex].prev = prevIndex;
		shadingBefore = shadingCurrent;

		prevIndex = curIndex;
		curIndex = LinkedListData[curIndex].next;
	}
	if (prevIndex != -1)
		LinkedListData[prevIndex].next = -1;
}
