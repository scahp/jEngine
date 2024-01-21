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

void jRaytracingScene_Vulkan::CreateOrUpdateBLAS(const jRatracingInitializer& InInitializer)
{
    auto CmdBuffer = (jCommandBuffer_Vulkan*)InInitializer.CommandBuffer;

    auto getBufferDeviceAddress = [](VkBuffer buffer)
        {
            VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
            bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            bufferDeviceAI.buffer = buffer;
            return g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &bufferDeviceAI);
        };

    std::vector<jRenderObject*> RTObjects;
    for (int32 i = 0; i < InInitializer.RenderObjects.size(); ++i)
    {
        jRenderObject* RObj = InInitializer.RenderObjects[i];
        check(RObj->IsSupportRaytracing());

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
            , EResourceLayout::GENERAL, &VertexIndexOffset, sizeof(Vector2i), TEXT("VertexAndIndexOffsetBuffer"));

        // Set GeometryDesc
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
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
        geometry.geometry.triangles.maxVertex = VertexCount;
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

        VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
        accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationDeviceAddressInfo.accelerationStructure = BLAS_Vulkan->AccelerationStructure;
        BLAS_Vulkan->DeviceAddress = g_rhi_vk->vkGetAccelerationStructureDeviceAddressKHR(g_rhi_vk->Device, &accelerationDeviceAddressInfo);

        InstanceList.push_back(RObj);
    }
}

void jRaytracingScene_Vulkan::CreateOrUpdateTLAS(const jRatracingInitializer& InInitializer)
{
    auto CmdBuffer = (jCommandBuffer_Vulkan*)InInitializer.CommandBuffer;

    const bool IsUpdate = !!ScratchTLASBufferPtr;

    if (!IsUpdate)
    {
        InstanceUploadBufferPtr = g_rhi->CreateStructuredBuffer(sizeof(VkAccelerationStructureInstanceKHR) * InstanceList.size()
            , sizeof(VkAccelerationStructureInstanceKHR), sizeof(VkAccelerationStructureInstanceKHR)
            , EBufferCreateFlag::CPUAccess | EBufferCreateFlag::AccelerationStructureBuildInput, EResourceLayout::UAV);
    }
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

        MappedPointer[i].instanceCustomIndex = i;
        MappedPointer[i].mask = 0xFF;
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
    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
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
        ScratchTLASBufferPtr = g_rhi->CreateRawBuffer(accelerationStructureBuildSizesInfo.buildScratchSize, 0, EBufferCreateFlag::UAV | EBufferCreateFlag::AccelerationStructureBuildInput, EResourceLayout::UAV);
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

    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = TLASBufferVulkan->AccelerationStructure;
    TLASBufferVulkan->DeviceAddress = g_rhi_vk->vkGetAccelerationStructureDeviceAddressKHR(g_rhi_vk->Device, &accelerationDeviceAddressInfo);
}
