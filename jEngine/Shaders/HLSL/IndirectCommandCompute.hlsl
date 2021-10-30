
#define ThreadBlockSize 128

struct SceneConstantBuffer
{
    float4 InstanceColor;
    float4 padding[15];         // to be aligned 256 byte.
};

struct IndirectCommand
{
    uint2 cbvAddress;
};

cbuffer RootConstants : register(b0)
{
    float commandCount; // The number of commands to be processed
};

StructuredBuffer<SceneConstantBuffer> cbv : register(t0);
StructuredBuffer<IndirectCommand> inputCommands : register(t1);
AppendStructuredBuffer<IndirectCommand> outputCommands : register(u0);

[numthreads(ThreadBlockSize, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    uint index = (groupId.x * ThreadBlockSize) + groupIndex;

    if (index < commandCount)
    {
        outputCommands.Append(inputCommands[index]);
    }
}