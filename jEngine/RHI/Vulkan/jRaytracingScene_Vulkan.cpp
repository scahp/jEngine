#include "pch.h"
#include "jRaytracingScene_Vulkan.h"
#include "jVertexBuffer_Vulkan.h"
#include "Scene/jRenderObject.h"
#include "Scene/jMeshObject.h"
#include "jIndexBuffer_Vulkan.h"
#include "jBuffer_Vulkan.h"
#include "jBufferUtil_Vulkan.h"
#include "jCommandBufferManager_Vulkan.h"
#include "jOptions.h"
#include "Material/jMaterial.h"

namespace
{
    static constexpr uint8 HWRTDI_InstanceMask_Scene = 0x01;
    static constexpr uint8 HWRTDI_InstanceMask_Sky = 0x02;
}

std::vector<jRenderObject*> jRaytracingScene_Vulkan::CollectBLASBuildWorkList(const jRatracingInitializer& InInitializer)
{
    std::vector<jRenderObject*> WorkList;
    std::unordered_set<jRenderObject*> Seen;
    const auto Append = [&](jRenderObject* Object) {
        if (Object && !PendingRemove.count(Object) && Object->IsSupportRaytracing() && Seen.insert(Object).second)
            WorkList.push_back(Object);
    };
    if (ShouldUpdate())
    {
        // Transform changes and explicit TLAS rebuilds do not change geometry.
        for (auto* Object : PendingAdd) Append(Object);
        for (auto* Object : DirtyBLAS) Append(Object);
    }
    else
    {
        InstanceList.clear();
        for (auto* Object : InInitializer.RenderObjects) Append(Object);
        bForceTLASRebuild = true;
    }
    return WorkList;
}

void jRaytracingScene_Vulkan::CreateOrUpdateBLAS(const jRatracingInitializer& InInitializer)
{
    auto CmdBuffer = (jCommandBuffer_Vulkan*)InInitializer.CommandBuffer;
    if (!CmdBuffer) return;

    auto getBufferDeviceAddress = [](VkBuffer buffer)
        {
            VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
            bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            bufferDeviceAI.buffer = buffer;
            return g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &bufferDeviceAI);
        };

    const auto WorkList = CollectBLASBuildWorkList(InInitializer);
    jBuffer_Vulkan* LastBuiltBLAS = nullptr;
    for (jRenderObject* RObj : WorkList)
    {
        // Remove Old BLAS
        RObj->BottomLevelASBuffer.reset();

        auto VertexBufferVulkanPtr = (jVertexBuffer_Vulkan*)RObj->GeometryDataPtr->VertexBuffer_PositionOnlyPtr.get();
        VkBuffer VtxBufferVulkan = VertexBufferVulkanPtr->BindInfos.Buffers[0];

        // Create VertexAndIndexOffsetBuffer
        VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
        vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(VtxBufferVulkan) + VertexBufferVulkanPtr->BindInfos.Offsets[0];

        uint64 VertexStart = vertexBufferDeviceAddress.deviceAddress;
        int32 VertexCount = VertexBufferVulkanPtr->GetElementCount();
        auto ROE = dynamic_cast<jRenderObjectElement*>(RObj);
        Vector2i VertexIndexOffset{};
        if (ROE)
        {
            VertexStart += VertexBufferVulkanPtr->Streams[0].Stride * ROE->SubMesh.StartVertex;
            VertexCount = ROE->SubMesh.EndVertex - ROE->SubMesh.StartVertex;

            VertexIndexOffset = Vector2i(ROE->SubMesh.StartVertex, ROE->SubMesh.StartFace);
        }

        RObj->VertexAndIndexOffsetBuffer = g_rhi->CreateStructuredBuffer(sizeof(Vector2i), 0, sizeof(Vector2i), EBufferCreateFlag::UAV | EBufferCreateFlag::CPUAccess
            , EResourceLayout::GENERAL, &VertexIndexOffset, sizeof(Vector2i), jNameStatic("VertexAndIndexOffsetBuffer"));

        // Set GeometryDesc
        const jMaterial* Material = RObj->MaterialPtr.get();
        const bool IsSkyMaterial = Material && Material->IsUseSphericalMap();
        const bool UseAlphaCutout = !IsSkyMaterial
            && Material
            && Material->HasAlbedoTexture()
            && Material->IsRaytracingAlphaTestEnabled();

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.flags = UseAlphaCutout ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
        //geometry.flags = 0;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VertexBufferVulkanPtr->BindInfos.AttributeDescriptions[0].format;

        uint64 IndexStart = 0;
        uint32 PrimitiveCount = 0;
        auto IndexBufferVulkan = (jIndexBuffer_Vulkan*)RObj->GeometryDataPtr->IndexBufferPtr.get();
        if (IndexBufferVulkan)
        {
            VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
            indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(IndexBufferVulkan->GetBuffer()->Buffer) + IndexBufferVulkan->GetBuffer()->Offset;

            auto& indexStreamData = IndexBufferVulkan->IndexStreamData;

            IndexStart = indexBufferDeviceAddress.deviceAddress;
            int32 IndexCount = IndexBufferVulkan->GetElementCount();
            if (ROE)
            {
                IndexStart += indexStreamData->Param->GetElementSize() * ROE->SubMesh.StartFace;
                IndexCount = ROE->SubMesh.EndFace - ROE->SubMesh.StartFace;
            }

            geometry.geometry.triangles.indexType = indexStreamData->Param->GetElementSize() == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
            geometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR(IndexStart);

            PrimitiveCount = IndexCount / 3;
        }
        else
        {
            PrimitiveCount = VertexCount / 3;
        }

        geometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR(VertexStart);
        geometry.geometry.triangles.maxVertex = std::max(0, VertexCount - 1);
        geometry.geometry.triangles.vertexStride = VertexBufferVulkanPtr->Streams[0].Stride;
        geometry.geometry.triangles.transformData.deviceAddress = 0;

        std::vector<VkAccelerationStructureGeometryKHR> geometries{ geometry };

        VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
        accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        accelerationStructureBuildGeometryInfo.geometryCount = (uint32)geometries.size();
        accelerationStructureBuildGeometryInfo.pGeometries = geometries.data();

        std::vector<uint32_t> maxPrimitiveCounts{ PrimitiveCount };

        // Create Raytracing PrebuildInfo
        VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
        accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        g_rhi_vk->vkGetAccelerationStructureBuildSizesKHR(
            g_rhi_vk->Device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &accelerationStructureBuildGeometryInfo,
            maxPrimitiveCounts.data(),
            &accelerationStructureBuildSizesInfo);

        jBuffer_Vulkan* BLAS_Vulkan = nullptr;
        {
            RObj->BottomLevelASBuffer = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(accelerationStructureBuildSizesInfo.accelerationStructureSize
                , 0, EBufferCreateFlag::AccelerationStructure, EResourceLayout::UAV);
            BLAS_Vulkan = RObj->GetBottomLevelASBuffer<jBuffer_Vulkan>();

            VkAccelerationStructureCreateInfoKHR accelerationStructureCreate_info{};
            accelerationStructureCreate_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelerationStructureCreate_info.buffer = BLAS_Vulkan->Buffer;
            accelerationStructureCreate_info.offset = BLAS_Vulkan->Offset;
            accelerationStructureCreate_info.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
            accelerationStructureCreate_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            g_rhi_vk->vkCreateAccelerationStructureKHR(g_rhi_vk->Device, &accelerationStructureCreate_info, nullptr, &BLAS_Vulkan->AccelerationStructure);
        }

        jBuffer_Vulkan* BLASScratch_Vulkan = nullptr;
        {
            RObj->ScratchASBuffer = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(accelerationStructureBuildSizesInfo.buildScratchSize, 0
                , EBufferCreateFlag::AccelerationStructureBuildInput | EBufferCreateFlag::UAV, EResourceLayout::TRANSFER_SRC);
            BLASScratch_Vulkan = RObj->GetScratchASBuffer<jBuffer_Vulkan>();

            VkBufferDeviceAddressInfoKHR scratchBufferDeviceAddresInfo{};
            scratchBufferDeviceAddresInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            scratchBufferDeviceAddresInfo.buffer = BLASScratch_Vulkan->Buffer;
            BLASScratch_Vulkan->DeviceAddress = g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &scratchBufferDeviceAddresInfo) + BLASScratch_Vulkan->Offset;
        }

        VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
        accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        accelerationBuildGeometryInfo.dstAccelerationStructure = BLAS_Vulkan->AccelerationStructure;
        accelerationBuildGeometryInfo.geometryCount = (uint32)geometries.size();
        accelerationBuildGeometryInfo.pGeometries = geometries.data();
        accelerationBuildGeometryInfo.scratchData.deviceAddress = BLASScratch_Vulkan->DeviceAddress;

        // Create BLAS
        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
        buildRangeInfo.firstVertex = 0;
        buildRangeInfo.primitiveOffset = 0;
        buildRangeInfo.primitiveCount = PrimitiveCount;
        buildRangeInfo.transformOffset = 0;

        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &buildRangeInfo };

        g_rhi_vk->vkCmdBuildAccelerationStructuresKHR(
            CmdBuffer->GetRef(),
            1,
            &accelerationBuildGeometryInfo,
            accelerationBuildStructureRangeInfos.data());

        LastBuiltBLAS = BLAS_Vulkan;

        VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
        accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationDeviceAddressInfo.accelerationStructure = BLAS_Vulkan->AccelerationStructure;
        BLAS_Vulkan->DeviceAddress = g_rhi_vk->vkGetAccelerationStructureDeviceAddressKHR(g_rhi_vk->Device, &accelerationDeviceAddressInfo);

        if (std::find(InstanceList.begin(), InstanceList.end(), RObj) == InstanceList.end())
            InstanceList.push_back(RObj);
        bForceTLASRebuild = true;
        PendingAdd.erase(RObj);
        DirtyBLAS.erase(RObj);
    }

    if (LastBuiltBLAS)
    {
        // This global dependency publishes all preceding BLAS builds for a TLAS
        // build, without serializing independent BLAS builds against each other.
        g_rhi->UAVBarrier(CmdBuffer, LastBuiltBLAS);
        CmdBuffer->FlushBarrierBatch();
    }
}

void jRaytracingScene_Vulkan::CreateOrUpdateTLAS(const jRatracingInitializer& InInitializer)
{
    auto CmdBuffer = (jCommandBuffer_Vulkan*)InInitializer.CommandBuffer;
    if (!CmdBuffer) return;

    for (auto* Object : PendingRemove)
    {
        // Removed objects may already have been destroyed by their scene owner.
        InstanceList.erase(std::remove(InstanceList.begin(), InstanceList.end(), Object), InstanceList.end());
        bForceTLASRebuild = true;
    }
    if (InstanceList.empty())
    {
        TLASBufferPtr.reset();
        ScratchTLASBufferPtr.reset();
        InstanceUploadBufferPtr.reset();
        LastTLASInstanceCount = 0;
        ClearDirtyFlags();
        BumpSceneUpdateIndex();
        return;
    }

    const bool IsUpdate = TLASBufferPtr && ScratchTLASBufferPtr && !bForceTLASRebuild
        && LastTLASInstanceCount == InstanceList.size();

    if (IsUpdate)
    {
        g_rhi->UAVBarrier(CmdBuffer, GetTLASBuffer<jBuffer_Vulkan>());
        // vkCmdBuildAccelerationStructuresKHR bypasses the RHI dispatch helpers
        // that normally flush pending barriers.
        CmdBuffer->FlushBarrierBatch();
    }
    // Keep prior instance bytes alive while earlier frames can still build from
    // them. The existing buffer allocator defers recycling released allocations.
    InstanceUploadBufferPtr = g_rhi->CreateStructuredBuffer(sizeof(VkAccelerationStructureInstanceKHR) * InstanceList.size()
        , sizeof(VkAccelerationStructureInstanceKHR), sizeof(VkAccelerationStructureInstanceKHR)
        , EBufferCreateFlag::CPUAccess | EBufferCreateFlag::AccelerationStructureBuildInput, EResourceLayout::UAV);
    auto InstanceUploadBufferVulkan = GetInstanceUploadBuffer<jBuffer_Vulkan>();

    VkBufferDeviceAddressInfoKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceDataDeviceAddress.buffer = InstanceUploadBufferVulkan->Buffer;
    InstanceUploadBufferVulkan->DeviceAddress
        = g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &instanceDataDeviceAddress) + InstanceUploadBufferVulkan->Offset;

    VkAccelerationStructureInstanceKHR* MappedPointer = (VkAccelerationStructureInstanceKHR*)InstanceUploadBufferVulkan->Map();
    for (int32 i = 0; i < InstanceList.size(); ++i)
    {
        jRenderObject* RObj = InstanceList[i];
        RObj->UpdateWorldMatrix();

        MappedPointer[i].instanceShaderBindingTableRecordOffset = RObj->RayTracingHitGroupIndex;
        MappedPointer[i].instanceCustomIndex = i;
        const jMaterial* Material = RObj->MaterialPtr.get();
        const bool IsSkyMaterial = Material && Material->IsUseSphericalMap();
        MappedPointer[i].mask = IsSkyMaterial ? HWRTDI_InstanceMask_Sky : HWRTDI_InstanceMask_Scene;
        MappedPointer[i].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
        MappedPointer[i].accelerationStructureReference = RObj->GetBottomLevelASBuffer<jBuffer_Vulkan>()->DeviceAddress;
        for (int32 k = 0; k < 3; ++k)
        {
            for (int32 m = 0; m < 4; ++m)
            {
                MappedPointer[i].transform.matrix[k][m] = RObj->World.m[m][k];
            }
        }
        if (gOptions.EarthQuake)
        {
            MappedPointer[i].transform.matrix[0][3] = sin((float)g_rhi->GetCurrentFrameNumber());
            MappedPointer[i].transform.matrix[1][3] = cos((float)g_rhi->GetCurrentFrameNumber());
            MappedPointer[i].transform.matrix[2][3] = sin((float)g_rhi->GetCurrentFrameNumber()) * 2;
        }

        //ASGeoInfos.push_back(accelerationStructureGeometry);
    }
    InstanceUploadBufferVulkan->Unmap();

    VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    //accelerationStructureGeometry.flags = 0;
    accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    accelerationStructureGeometry.geometry.instances.data
        = VkDeviceOrHostAddressConstKHR(InstanceUploadBufferVulkan->DeviceAddress);

    // Get Size info
    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
    accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

    uint32 primitive_count = (uint32)InstanceList.size();

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
    accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g_rhi_vk->vkGetAccelerationStructureBuildSizesKHR(
        g_rhi_vk->Device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo,
        &primitive_count,
        &accelerationStructureBuildSizesInfo);

    if (!IsUpdate)
    {
        TLASBufferPtr = g_rhi->CreateRawBuffer(accelerationStructureBuildSizesInfo.accelerationStructureSize, 0, EBufferCreateFlag::AccelerationStructure, EResourceLayout::UAV);
    }
    auto TLASBufferVulkan = (jBuffer_Vulkan*)TLASBufferPtr.get();

    if (!TLASBufferVulkan->AccelerationStructure)
    {
        VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
        accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelerationStructureCreateInfo.buffer = TLASBufferVulkan->Buffer;
        accelerationStructureCreateInfo.offset = TLASBufferVulkan->Offset;
        accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
        accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        g_rhi_vk->vkCreateAccelerationStructureKHR(g_rhi_vk->Device, &accelerationStructureCreateInfo, nullptr, &TLASBufferVulkan->AccelerationStructure);
    }

    // Create a small scratch buffer used during build of the bottom level acceleration structure
    if (!IsUpdate)
    {
        ScratchTLASBufferPtr = g_rhi->CreateRawBuffer(Max(accelerationStructureBuildSizesInfo.buildScratchSize,
            accelerationStructureBuildSizesInfo.updateScratchSize), 0,
            EBufferCreateFlag::UAV | EBufferCreateFlag::AccelerationStructureBuildInput, EResourceLayout::UAV);
    }
    auto ScratchASBufferVulkan = (jBuffer_Vulkan*)ScratchTLASBufferPtr.get();

    VkBufferDeviceAddressInfoKHR scratchBufferDeviceAddresInfo{};
    scratchBufferDeviceAddresInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferDeviceAddresInfo.buffer = ScratchASBufferVulkan->Buffer;
    ScratchASBufferVulkan->DeviceAddress = g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &scratchBufferDeviceAddresInfo) + ScratchASBufferVulkan->Offset;

    VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
    accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    accelerationBuildGeometryInfo.mode = IsUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationBuildGeometryInfo.srcAccelerationStructure = IsUpdate ? TLASBufferVulkan->AccelerationStructure : nullptr;
    accelerationBuildGeometryInfo.dstAccelerationStructure = TLASBufferVulkan->AccelerationStructure;
    accelerationBuildGeometryInfo.geometryCount = 1;
    accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
    accelerationBuildGeometryInfo.scratchData.deviceAddress = ScratchASBufferVulkan->DeviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
    accelerationStructureBuildRangeInfo.primitiveCount = primitive_count;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

    g_rhi_vk->vkCmdBuildAccelerationStructuresKHR(
        CmdBuffer->GetRef(),
        1,
        &accelerationBuildGeometryInfo,
        accelerationBuildStructureRangeInfos.data());

    g_rhi->UAVBarrier(CmdBuffer, TLASBufferVulkan);
    CmdBuffer->FlushBarrierBatch();

    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = TLASBufferVulkan->AccelerationStructure;
    TLASBufferVulkan->DeviceAddress = g_rhi_vk->vkGetAccelerationStructureDeviceAddressKHR(g_rhi_vk->Device, &accelerationDeviceAddressInfo);

    LastTLASInstanceCount = primitive_count;
    ClearDirtyFlags();
    BumpSceneUpdateIndex();
}
