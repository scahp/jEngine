#pragma once
#include "jRHIType_Vulkan.h"
#include "jBuffer_Vulkan.h"

struct jVertexStream_Vulkan
{
    jName Name;
    //uint32 Count;
    EBufferType BufferType = EBufferType::STATIC;
    //EBufferElementType ElementType = EBufferElementType::BYTE;
    bool Normalized = false;
    int32 Stride = 0;
    size_t Offset = 0;
    int32 InstanceDivisor = 0;

    std::shared_ptr<jBuffer_Vulkan> BufferPtr;
};

struct jVertexBuffer_Vulkan : public jVertexBuffer
{
    struct jBindInfo
    {
        void Reset()
        {
            InputBindingDescriptions.clear();
            AttributeDescriptions.clear();
            Buffers.clear();
            Offsets.clear();
            StartBindingIndex = 0;
        }
        std::vector<VkVertexInputBindingDescription> InputBindingDescriptions;
        std::vector<VkVertexInputAttributeDescription> AttributeDescriptions;
        
        // Buffer 는 jVertexStream_Vulkan 을 레퍼런스 하고 있기 때문에 별도 소멸 필요 없음
        std::vector<VkBuffer> Buffers;
        std::vector<VkDeviceSize> Offsets;

        int32 StartBindingIndex = 0;

        VkPipelineVertexInputStateCreateInfo CreateVertexInputState() const
        {
            // Vertex Input
            // 1). Bindings : 데이터 사이의 간격과 버택스당 or 인스턴스당(인스턴싱 사용시) 데이터인지 여부
            // 2). Attribute descriptions : 버택스 쉐이더 전달되는 attributes 의 타입. 그것을 로드할 바인딩과 오프셋
            VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

            vertexInputInfo.vertexBindingDescriptionCount = (uint32)InputBindingDescriptions.size();
            vertexInputInfo.pVertexBindingDescriptions = &InputBindingDescriptions[0];
            vertexInputInfo.vertexAttributeDescriptionCount = (uint32)AttributeDescriptions.size();;
            vertexInputInfo.pVertexAttributeDescriptions = &AttributeDescriptions[0];

            return vertexInputInfo;
        }

        FORCEINLINE size_t GetHash() const
        {
            size_t result = CityHash64((const char*)InputBindingDescriptions.data(), sizeof(VkVertexInputBindingDescription) * InputBindingDescriptions.size());
            result = CityHash64WithSeed((const char*)AttributeDescriptions.data(), sizeof(VkVertexInputAttributeDescription) * AttributeDescriptions.size(), result);
            return result;
        }
    };

    virtual size_t GetHash() const override
    {
        if (Hash)
            return Hash;

        Hash = GetVertexInputStateHash() ^ GetInputAssemblyStateHash();
        return Hash;
    }

    FORCEINLINE size_t GetVertexInputStateHash() const
    {
        return BindInfos.GetHash();
    }

    FORCEINLINE size_t GetInputAssemblyStateHash() const
    {
        VkPipelineInputAssemblyStateCreateInfo state = CreateInputAssemblyState();
        return CityHash64((const char*)&state, sizeof(VkPipelineInputAssemblyStateCreateInfo));
    }

    FORCEINLINE VkPipelineVertexInputStateCreateInfo CreateVertexInputState() const
    {
        return BindInfos.CreateVertexInputState();
    }

    FORCEINLINE VkPipelineInputAssemblyStateCreateInfo CreateInputAssemblyState() const
    {
        check(VertexStreamData);

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = GetVulkanPrimitiveTopology(VertexStreamData->PrimitiveType);

        // primitiveRestartEnable 옵션이 VK_TRUE 이면, 인덱스버퍼의 특수한 index 0xFFFF or 0xFFFFFFFF 를 사용해서 line 과 triangle topology mode를 사용할 수 있다.
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        return inputAssembly;
    }

    static void CreateVertexInputState(VkPipelineVertexInputStateCreateInfo& OutVertexInputInfo,
        std::vector<VkVertexInputBindingDescription>& OutBindingDescriptions, std::vector<VkVertexInputAttributeDescription>& OutAttributeDescriptions,
        std::vector<const jVertexBuffer*> InVertexBuffers)
    {
        for (int32 i = 0; i < (int32)InVertexBuffers.size(); ++i)
        {
            if (InVertexBuffers[i])
            {
                const jBindInfo& bindInfo = ((const jVertexBuffer_Vulkan*)InVertexBuffers[i])->BindInfos;
                OutBindingDescriptions.insert(OutBindingDescriptions.end(), bindInfo.InputBindingDescriptions.begin(), bindInfo.InputBindingDescriptions.end());
                OutAttributeDescriptions.insert(OutAttributeDescriptions.end(), bindInfo.AttributeDescriptions.begin(), bindInfo.AttributeDescriptions.end());
            }
        }
        
        OutVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        OutVertexInputInfo.vertexBindingDescriptionCount = (uint32)OutBindingDescriptions.size();
        OutVertexInputInfo.pVertexBindingDescriptions = OutBindingDescriptions.data();
        OutVertexInputInfo.vertexAttributeDescriptionCount = (uint32)OutAttributeDescriptions.size();;
        OutVertexInputInfo.pVertexAttributeDescriptions = OutAttributeDescriptions.data();
    }

    jBindInfo BindInfos;
    std::vector<jVertexStream_Vulkan> Streams;
    mutable size_t Hash = 0;

    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;
};
