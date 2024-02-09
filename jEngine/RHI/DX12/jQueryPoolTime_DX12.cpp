#include "pch.h"
#include "jQueryPoolTime_DX12.h"
#include "Profiler/jPerformanceProfile.h"
#include "jBufferUtil_DX12.h"

//const uint32 MaxQueryTimeCount = 512;

//////////////////////////////////////////////////////////////////////////
// jQueryPoolTime_DX12
//////////////////////////////////////////////////////////////////////////
bool jQueryPoolTime_DX12::Create()
{
    const uint64 ReadbackBufferSize = MaxQueryTimeCount * jRHI::MaxWaitingQuerySet * sizeof(uint64);
    ReadbackBuffer = std::shared_ptr<jBuffer_DX12>(jBufferUtil_DX12::CreateBuffer(ReadbackBufferSize, 0, EBufferCreateFlag::Readback
        , EResourceLayout::TRANSFER_DST, nullptr, ReadbackBufferSize, TEXT("QueryPoolTime_ReadbackBuffer")));

    D3D12_QUERY_HEAP_DESC heapDesc = { };
    heapDesc.Count = MaxQueryTimeCount * jRHI::MaxWaitingQuerySet;
    heapDesc.NodeMask = 0;
    heapDesc.Type = (CommandBufferType == ECommandBufferType::COPY) ? D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    return JOK(g_rhi_dx12->Device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&QueryHeap)));
}

void jQueryPoolTime_DX12::ResetQueryPool(jCommandBuffer* pCommanBuffer /*= nullptr*/)
{
    QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] = MaxQueryTimeCount * jProfile_GPU::CurrentWatingResultListIndex;
}

void jQueryPoolTime_DX12::Release()
{
    ReleaseInstance();
}

int32 jQueryPoolTime_DX12::GetUsedQueryCount(int32 InFrameIndex) const
{
    return QueryIndex[InFrameIndex] - InFrameIndex * MaxQueryTimeCount;
}

std::vector<uint64> jQueryPoolTime_DX12::GetWholeQueryResult(int32 InFrameIndex, int32 InCount) const
{
    check(ReadbackBuffer);

    std::vector<uint64> queryResult;
    queryResult.resize(InCount);
    const uint64 dstOffset = (InFrameIndex * MaxQueryTimeCount);

    uint64* MappedPtr = (uint64*)ReadbackBuffer->Map();
    MappedPtr += dstOffset;
    memcpy(&queryResult[0], MappedPtr, InCount * sizeof(uint64));
    ReadbackBuffer->Unmap();

    return queryResult;
}

std::vector<uint64> jQueryPoolTime_DX12::GetWholeQueryResult(int32 InFrameIndex) const
{
    check(ReadbackBuffer);

    std::vector<uint64> queryResult;
    queryResult.resize(MaxQueryTimeCount);
    const uint64 dstOffset = (InFrameIndex * MaxQueryTimeCount);

    uint64* MappedPtr = (uint64*)ReadbackBuffer->Map();
    MappedPtr += dstOffset;
    memcpy(&queryResult[0], MappedPtr, MaxQueryTimeCount * sizeof(uint64));
    ReadbackBuffer->Unmap();

    return queryResult;
}

void jQueryPoolTime_DX12::ReleaseInstance()
{
    QueryHeap.Reset();
}

//////////////////////////////////////////////////////////////////////////
// jQueryTime_DX12
//////////////////////////////////////////////////////////////////////////
void jQueryTime_DX12::Init()
{
    auto QueryPoolTime = (jQueryPoolTime_DX12*)g_rhi->GetQueryTimePool(GetCommandBufferType());
    QueryId = QueryPoolTime->QueryIndex[jProfile_GPU::CurrentWatingResultListIndex];
    QueryPoolTime->QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] += 2;		// Need 2 Queries for Starting, Ending timestamp
}

void jQueryTime_DX12::BeginQuery(const jCommandBuffer* commandBuffer) const
{
    check(commandBuffer->Type == GetCommandBufferType());

    auto commandBuffer_DX12 = (jCommandBuffer_DX12*)commandBuffer;
    check(commandBuffer_DX12);
    check(commandBuffer_DX12->CommandList);

    auto QueryPoolTime = (jQueryPoolTime_DX12*)g_rhi->GetQueryTimePool(GetCommandBufferType());
    commandBuffer_DX12->CommandList->EndQuery(QueryPoolTime->QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryId);
}

void jQueryTime_DX12::EndQuery(const jCommandBuffer* commandBuffer) const
{
    check(commandBuffer->Type == GetCommandBufferType());

    auto commandBuffer_DX12 = (jCommandBuffer_DX12*)commandBuffer;
    check(commandBuffer_DX12);
    check(commandBuffer_DX12->CommandList);

    auto QueryPoolTime = (jQueryPoolTime_DX12*)g_rhi->GetQueryTimePool(GetCommandBufferType());
    commandBuffer_DX12->CommandList->EndQuery(QueryPoolTime->QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryId + 1);
    commandBuffer_DX12->CommandList->ResolveQueryData(QueryPoolTime->QueryHeap.Get()
        , D3D12_QUERY_TYPE_TIMESTAMP, QueryId, 2, QueryPoolTime->ReadbackBuffer->Buffer->Get(), QueryId * sizeof(uint64));
}

bool jQueryTime_DX12::IsQueryTimeStampResult(bool isWaitUntilAvailable) const
{
    check(0);
    return true;
}

void jQueryTime_DX12::GetQueryResult() const
{
    check(0);
}

void jQueryTime_DX12::GetQueryResultFromQueryArray(int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryArray) const
{
    const uint32 queryStart = (QueryId) - InWatingResultIndex * MaxQueryTimeCount;
    const uint32 queryEnd = (QueryId + 1) - InWatingResultIndex * MaxQueryTimeCount;
    check(queryStart >= 0 && queryStart < MaxQueryTimeCount);
    check(queryEnd >= 0 && queryEnd < MaxQueryTimeCount);

    TimeStampStartEnd[0] = wholeQueryArray[queryStart];
    TimeStampStartEnd[1] = wholeQueryArray[queryEnd];
}