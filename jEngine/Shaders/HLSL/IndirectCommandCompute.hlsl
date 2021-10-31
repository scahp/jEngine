
#define ThreadBlockSize 128

struct InstanceConstantBuffer
{
    float4 InstanceColor;
    float4 padding[15];         // to be aligned 256 byte.
};

struct MVPBuffer
{
    float4x4 MVP;
    float4 padding[12];         // to be aligned 256 byte.
};

struct IndirectCommand
{
    uint2 Instance_cbvAddress;
    uint2 MVP_cbvAddress;
    
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
    
    float padding; // align to multiple of 8
    //float4 padding[55]; // to be aligned 256 byte.
};

cbuffer RootConstants : register(b0)
{
    float commandCount; // The number of commands to be processed
};

StructuredBuffer<InstanceConstantBuffer> InstanceCBV : register(t0);
StructuredBuffer<MVPBuffer> MVPCBV : register(t1);
StructuredBuffer<IndirectCommand> inputCommands : register(t2);
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