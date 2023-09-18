#include "pch.h"
#include "jVertexBuffer_Vulkan.h"
#include "../jRenderFrameContext.h"

void jVertexBuffer_Vulkan::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
    vkCmdBindVertexBuffers((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), BindInfos.StartBindingIndex
        , (uint32)BindInfos.Buffers.size(), &BindInfos.Buffers[0], &BindInfos.Offsets[0]);
}

bool jVertexBuffer_Vulkan::Initialize(const std::shared_ptr<jVertexStreamData>& InStreamData)
{
    if (!InStreamData)
        return false;

    VertexStreamData = InStreamData;
    BindInfos.Reset();
    BindInfos.StartBindingIndex = InStreamData->BindingIndex;

    std::list<uint32> buffers;
    int32 locationIndex = InStreamData->StartLocation;
    int32 bindingIndex = InStreamData->BindingIndex;
    for (const auto& iter : InStreamData->Params)
    {
        if (iter->Stride <= 0)
            continue;

        jVertexStream_Vulkan stream;
        stream.Name = iter->Name;
        stream.BufferType = EBufferType::STATIC;
        stream.Stride = iter->Stride;
        stream.Offset = 0;
        stream.InstanceDivisor = iter->InstanceDivisor;

        if (iter->GetBufferSize() > 0)
        {
            VkDeviceSize bufferSize = iter->GetBufferSize();
            jBuffer_Vulkan stagingBuffer;

            // VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 이 버퍼가 메모리 전송 연산의 소스가 될 수 있음.
            jVulkanBufferUtil::AllocateBuffer(EVulkanBufferBits::TRANSFER_SRC
                , EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT, bufferSize, stagingBuffer);

            //// 마지막 파라메터 0은 메모리 영역의 offset 임.
            //// 이 값이 0이 아니면 memRequirements.alignment 로 나눠야 함. (align 되어있다는 의미)
            //vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

            stagingBuffer.UpdateBuffer(iter->GetBufferData(), bufferSize);

            // Map -> Unmap 했다가 메모리에 데이터가 즉시 반영되는게 아님
            // 바로 사용하려면 아래 2가지 방법이 있음.
            // 1. VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 사용 (항상 반영, 약간 느릴 수도)
            // 2. 쓰기 이후 vkFlushMappedMemoryRanges 호출, 읽기 이후 vkInvalidateMappedMemoryRanges 호출
            // 위의 2가지 방법을 사용해도 이 데이터가 GPU에 바로 보인다고 보장할 수는 없지만 다음 vkQueueSubmit 호출 전에는 완료될 것을 보장함.

            // VK_BUFFER_USAGE_TRANSFER_DST_BIT : 이 버퍼가 메모리 전송 연산의 목적지가 될 수 있음.
            // DEVICE LOCAL 메모리에 VertexBuffer를 만들었으므로 이제 vkMapMemory 같은 것은 할 수 없음.
            stream.BufferPtr = std::make_shared<jBuffer_Vulkan>();
            jVulkanBufferUtil::AllocateBuffer(EVulkanBufferBits::TRANSFER_DST | EVulkanBufferBits::VERTEX_BUFFER
                , EVulkanMemoryBits::DEVICE_LOCAL, bufferSize, *stream.BufferPtr.get());

            jVulkanBufferUtil::CopyBuffer(stagingBuffer, *stream.BufferPtr.get(), bufferSize);

            stagingBuffer.Release();

            BindInfos.Buffers.push_back(stream.BufferPtr->Buffer);
            BindInfos.Offsets.push_back(stream.Offset + stream.BufferPtr->Offset);
        }

        /////////////////////////////////////////////////////////////
        VkVertexInputBindingDescription bindingDescription = {};
        // 모든 데이터가 하나의 배열에 있어서 binding index는 0
        bindingDescription.binding = bindingIndex;
        bindingDescription.stride = iter->Stride;

        // VK_VERTEX_INPUT_RATE_VERTEX : 각각 버택스 마다 다음 데이터로 이동
        // VK_VERTEX_INPUT_RATE_INSTANCE : 각각의 인스턴스 마다 다음 데이터로 이동
        bindingDescription.inputRate = GetVulkanVertexInputRate(InStreamData->VertexInputRate);
        BindInfos.InputBindingDescriptions.push_back(bindingDescription);
        /////////////////////////////////////////////////////////////

        int32 ElementStride = 0;
        for (IStreamParam::jAttribute& element : iter->Attributes)
        {
            VkVertexInputAttributeDescription attributeDescription = {};
            attributeDescription.binding = bindingIndex;
            attributeDescription.location = locationIndex;

            VkFormat AttrFormat = VK_FORMAT_UNDEFINED;
            switch (element.UnderlyingType)
            {
            case EBufferElementType::BYTE:
            {
                const int32 elementCount = element.Stride / sizeof(char);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = VK_FORMAT_R8_SINT;
                    break;
                case 2:
                    AttrFormat = VK_FORMAT_R8G8_SINT;
                    break;
                case 3:
                    AttrFormat = VK_FORMAT_R8G8B8_SINT;
                    break;
                case 4:
                    AttrFormat = VK_FORMAT_R8G8B8A8_SINT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::BYTE_UNORM:
            {
                const int32 elementCount = element.Stride / sizeof(char);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = VK_FORMAT_R8_UNORM;
                    break;
                case 2:
                    AttrFormat = VK_FORMAT_R8G8_UNORM;
                    break;
                case 3:
                    AttrFormat = VK_FORMAT_R8G8B8_UNORM;
                    break;
                case 4:
                    AttrFormat = VK_FORMAT_R8G8B8A8_UNORM;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::UINT16:
            {
                const int32 elementCount = element.Stride / sizeof(uint16);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = VK_FORMAT_R16_UINT;
                    break;
                case 2:
                    AttrFormat = VK_FORMAT_R16G16_UINT;
                    break;
                case 3:
                    AttrFormat = VK_FORMAT_R16G16B16_UINT;
                    break;
                case 4:
                    AttrFormat = VK_FORMAT_R16G16B16A16_UINT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::UINT32:
            {
                const int32 elementCount = element.Stride / sizeof(uint32);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = VK_FORMAT_R32_UINT;
                    break;
                case 2:
                    AttrFormat = VK_FORMAT_R32G32_UINT;
                    break;
                case 3:
                    AttrFormat = VK_FORMAT_R32G32B32_UINT;
                    break;
                case 4:
                    AttrFormat = VK_FORMAT_R32G32B32A32_UINT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            case EBufferElementType::FLOAT:
            {
                const int32 elementCount = element.Stride / sizeof(float);
                switch (elementCount)
                {
                case 1:
                    AttrFormat = VK_FORMAT_R32_SFLOAT;
                    break;
                case 2:
                    AttrFormat = VK_FORMAT_R32G32_SFLOAT;
                    break;
                case 3:
                    AttrFormat = VK_FORMAT_R32G32B32_SFLOAT;
                    break;
                case 4:
                    AttrFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
                    break;
                default:
                    check(0);
                    break;
                }
                break;
            }
            default:
                check(0);
                break;
            }
            check(AttrFormat != VK_FORMAT_UNDEFINED);
            //float: VK_FORMAT_R32_SFLOAT
            //vec2 : VK_FORMAT_R32G32_SFLOAT
            //vec3 : VK_FORMAT_R32G32B32_SFLOAT
            //vec4 : VK_FORMAT_R32G32B32A32_SFLOAT
            //ivec2: VK_FORMAT_R32G32_SINT, a 2-component vector of 32-bit signed integers
            //uvec4: VK_FORMAT_R32G32B32A32_UINT, a 4-component vector of 32-bit unsigned integers
            //double: VK_FORMAT_R64_SFLOAT, a double-precision (64-bit) float
            attributeDescription.format = AttrFormat;
            attributeDescription.offset = ElementStride;
            BindInfos.AttributeDescriptions.push_back(attributeDescription);

            ElementStride += element.Stride;
            ++locationIndex;
        }
        /////////////////////////////////////////////////////////////
        Streams.emplace_back(stream);

        ++bindingIndex;
    }

    GetHash();		// GenerateHash
    return true;
}
