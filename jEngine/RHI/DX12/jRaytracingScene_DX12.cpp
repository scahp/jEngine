#include "pch.h"
#include "jRaytracingScene_DX12.h"
#include "Scene/jRenderObject.h"
#include "jCommandBufferManager_DX12.h"
#include "Scene/jMeshObject.h"
#include "jIndexBuffer_DX12.h"
#include "jVertexBuffer_DX12.h"
#include "jBufferUtil_DX12.h"
#include "jOptions.h"

void jRaytracingScene_DX12::CreateOrUpdateBLAS(const jRatracingInitializer& InInitializer)
{
    auto CmdBuffer = (jCommandBuffer_DX12*)InInitializer.CommandBuffer;
    std::vector<jRenderObject*> RTObjects;
    for (int32 i = 0; i < InInitializer.RenderObjects.size(); ++i)
    {
        jRenderObject* RObj = InInitializer.RenderObjects[i];
        check(RObj->IsSupportRaytracing());

        // Remove Old BLAS
        if (RObj->BottomLevelASBuffer)
        {
            auto temp = CD3DX12_RESOURCE_BARRIER::UAV(((jBuffer_DX12*)RObj->BottomLevelASBuffer)->Buffer->Get());
            CmdBuffer->CommandList->ResourceBarrier(1, &temp);

            delete RObj->BottomLevelASBuffer;
            RObj->BottomLevelASBuffer = nullptr;
        }

        auto VertexBuffer_PositionOnly = RObj->GeometryDataPtr->VertexBuffer_PositionOnly;
        check(VertexBuffer_PositionOnly->VertexStreamData->Params.size() == 1);
        check(VertexBuffer_PositionOnly->VertexStreamData->Params[0]->Stride == 4 * 3);

        auto VertexBufferDX12 = (jVertexBuffer_DX12*)VertexBuffer_PositionOnly;

        // Create VertexAndIndexOffsetBuffer
        ID3D12Resource* VtxBufferDX12 = VertexBufferDX12->BindInfos.Buffers[0];
        D3D12_GPU_VIRTUAL_ADDRESS VertexStart = VtxBufferDX12->GetGPUVirtualAddress();
        int32 VertexCount = VertexBufferDX12->GetElementCount();
        auto ROE = dynamic_cast<jRenderObjectElement*>(RObj);
        Vector2i VertexIndexOffset{};
        if (ROE)
        {
            VertexStart += VertexBufferDX12->Streams[0].Stride * ROE->SubMesh.StartVertex;
            VertexCount = ROE->SubMesh.EndVertex - ROE->SubMesh.StartVertex + 1;

            VertexIndexOffset = Vector2i(ROE->SubMesh.StartVertex, ROE->SubMesh.StartFace);
        }
        if (RObj->VertexAndIndexOffsetBuffer)
            delete RObj->VertexAndIndexOffsetBuffer;

        RObj->VertexAndIndexOffsetBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(Vector2i), 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_COMMON
            , &VertexIndexOffset, sizeof(Vector2i), TEXT("VertexAndIndexOffsetBuffer"));
        jBufferUtil_DX12::CreateShaderResourceView((jBuffer_DX12*)RObj->VertexAndIndexOffsetBuffer, sizeof(Vector2i), 1);

        // Set GeometryDesc
        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        auto IndexBuffer = RObj->GeometryDataPtr->IndexBuffer;
        if (IndexBuffer)
        {
            auto IndexBufferDX12 = (jIndexBuffer_DX12*)IndexBuffer;
            ComPtr<ID3D12Resource> IdxBufferDX12 = IndexBufferDX12->BufferPtr->Buffer->Resource;
            auto& indexStreamData = IndexBufferDX12->IndexStreamData;

            D3D12_GPU_VIRTUAL_ADDRESS IndexStart = IndexBufferDX12->BufferPtr->GetGPUAddress();
            int32 IndexCount = IndexBuffer->GetElementCount();
            if (ROE)
            {
                IndexStart += indexStreamData->Param->GetElementSize() * ROE->SubMesh.StartFace;
                IndexCount = ROE->SubMesh.EndFace - ROE->SubMesh.StartFace + 1;
            }

            geometryDesc.Triangles.IndexBuffer = IndexStart;
            geometryDesc.Triangles.IndexCount = IndexCount;
            geometryDesc.Triangles.IndexFormat = indexStreamData->Param->GetElementSize() == 16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        }
        else
        {
            geometryDesc.Triangles.IndexBuffer = {};
            geometryDesc.Triangles.IndexCount = 0;
            geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
        }
        geometryDesc.Triangles.Transform3x4 = 0;
        geometryDesc.Triangles.VertexFormat = VertexBufferDX12->BindInfos.InputElementDescs[0].Format;
        geometryDesc.Triangles.VertexCount = VertexCount;
        geometryDesc.Triangles.VertexBuffer.StartAddress = VertexStart;
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = VertexBufferDX12->Streams[0].Stride;

        // Opaque로 지오메트를 등록하면, 히트 쉐이더에서 더이상 쉐이더를 만들지 않을 것이므로 최적화에 좋다.
        //geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        // Create Raytracing PrebuildInfo
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo{};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs{};
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottomLevelInputs.Flags = buildFlags;
        bottomLevelInputs.pGeometryDescs = &geometryDesc;
        bottomLevelInputs.NumDescs = 1;
        bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

        g_rhi_dx12->Device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
        if (!JASSERT(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0))
            continue;

        D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        delete RObj->BottomLevelASBuffer;
        RObj->BottomLevelASBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, 0, EBufferCreateFlag::UAV, initialResourceState
            , nullptr, 0, TEXT("BottomLevelAccelerationStructure"));

        delete RObj->ScratchASBuffer;
        RObj->ScratchASBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfo.ScratchDataSizeInBytes, 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_COMMON
            , nullptr, 0, TEXT("ScratchResourceGeometry"));

        // Create BLAS
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc{};
        bottomLevelBuildDesc.Inputs = bottomLevelInputs;
        bottomLevelBuildDesc.ScratchAccelerationStructureData = ((jBuffer_DX12*)RObj->ScratchASBuffer)->GetGPUAddress();
        bottomLevelBuildDesc.DestAccelerationStructureData = ((jBuffer_DX12*)RObj->BottomLevelASBuffer)->GetGPUAddress();

        CmdBuffer->CommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
        auto temp = CD3DX12_RESOURCE_BARRIER::UAV(((jBuffer_DX12*)RObj->BottomLevelASBuffer)->Buffer->Get());
        CmdBuffer->CommandList->ResourceBarrier(1, &temp);

        InstanceList.push_back(RObj);
    }
}

void jRaytracingScene_DX12::CreateOrUpdateTLAS(const jRatracingInitializer& InInitializer)
{
    if (InstanceList.size() <= 0)
        return;

    auto CmdBuffer = (jCommandBuffer_DX12*)InInitializer.CommandBuffer;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
                    | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = (uint32)InstanceList.size();
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    g_rhi_dx12->Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    check(info.ResultDataMaxSizeInBytes);

    const bool IsUpdate = !!ScratchTLASBuffer;
    if (IsUpdate)
    {
        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = ((jBuffer_DX12*)TLASBuffer)->Buffer->Get();
        CmdBuffer->CommandList->ResourceBarrier(1, &uavBarrier);
    }
    else
    {
        ScratchTLASBuffer = jBufferUtil_DX12::CreateBuffer(info.ScratchDataSizeInBytes, 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_COMMON
            , nullptr, 0, TEXT("TLAS Scratch Buffer"));

        TLASBuffer = jBufferUtil_DX12::CreateBuffer(info.ResultDataMaxSizeInBytes, 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
            , nullptr, 0, TEXT("TLAS Result Buffer"));

        InstanceUploadBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * InstanceList.size(), 0
            , EBufferCreateFlag::CPUAccess, D3D12_RESOURCE_STATE_GENERIC_READ
            , nullptr, 0, TEXT("TLAS Result Buffer"));
    }

    if (D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = (D3D12_RAYTRACING_INSTANCE_DESC*)InstanceUploadBuffer->Map())
    {
        for (int32 i = 0; i < (int32)InstanceList.size(); ++i)
        {
            jRenderObject* RObj = InstanceList[i];
            RObj->UpdateWorldMatrix();

            instanceDescs[i].InstanceID = i;
            instanceDescs[i].InstanceContributionToHitGroupIndex = 0;
            instanceDescs[i].InstanceMask = 0xFF;
            instanceDescs[i].AccelerationStructure = ((jBuffer_DX12*)RObj->BottomLevelASBuffer)->GetGPUAddress();
            for (int32 k = 0; k < 3; ++k)
            {
                for (int32 m = 0; m < 4; ++m)
                {
                    instanceDescs[i].Transform[k][m] = RObj->World.m[m][k];
                }
            }
            if (gOptions.EarthQuake)
            {
                instanceDescs[i].Transform[0][3] = sin((float)g_rhi->GetCurrentFrameNumber());
                instanceDescs[i].Transform[1][3] = cos((float)g_rhi->GetCurrentFrameNumber());
                instanceDescs[i].Transform[2][3] = sin((float)g_rhi->GetCurrentFrameNumber()) * 2;
            }
            instanceDescs[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
        }
        InstanceUploadBuffer->Unmap();
    }
    else
    {
        int32 i = 0;
        ++i;
        return;
    }

    // TLAS 생성
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.Inputs.InstanceDescs = ((jBuffer_DX12*)InstanceUploadBuffer)->GetGPUAddress();
    asDesc.DestAccelerationStructureData = ((jBuffer_DX12*)TLASBuffer)->GetGPUAddress();
    asDesc.ScratchAccelerationStructureData = ((jBuffer_DX12*)ScratchTLASBuffer)->GetGPUAddress();

    if (IsUpdate)
    {
        asDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        asDesc.SourceAccelerationStructureData = ((jBuffer_DX12*)TLASBuffer)->Buffer->GetGPUVirtualAddress();
    }

    CmdBuffer->CommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
    auto temp = CD3DX12_RESOURCE_BARRIER::UAV(((jBuffer_DX12*)TLASBuffer)->Buffer->Get());
    CmdBuffer->CommandList->ResourceBarrier(1, &temp);
}
