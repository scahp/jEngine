#include "pch.h"
#include "jTexture_Vulkan.h"

void jTexture_Vulkan::Release()
{
    ReleaseInternal();
}

void jTexture_Vulkan::ReleaseInternal()
{
    check(g_rhi_vk);
    check(g_rhi_vk->Device);

    // Swapchain 으로 만든 image 인 경우 Memory 가 nullptr 이기 때문에 Image 와 Memory 소멸 필요 없음.
    // View 의 경우 직접 생성한 것이므로 소멸 필요
    if (Memory)
    {
        std::set<VkImageView> ViewSets;

        // View, ViewUAV is included in ViewForMipMap, ViewUAVForMipMap, if containers are not empty.
        if (ViewForMipMap.empty())
        {
            if (View)
                ViewSets.insert(View);
        }
        else
        {
            for(auto it : ViewForMipMap)
                ViewSets.insert(it.second);
        }

        if (ViewUAVForMipMap.empty())
        {
			if (ViewUAV)
                ViewSets.insert(ViewUAV);
        }
        else
        {
		    for (auto it : ViewUAVForMipMap)
                ViewSets.insert(it.second);
        }

        jStandaloneResourceVulkan::ReleaseImageResource(Image, Memory, std::vector<VkImageView>(ViewSets.begin(), ViewSets.end()));
    }
    Image = nullptr;
    Memory = nullptr;
    View = nullptr;
    ViewUAV = nullptr;
    ViewForMipMap.clear();
    ViewUAVForMipMap.clear();

}

static VkSampler g_defaultSampler = nullptr;

VkSampler jTexture_Vulkan::CreateDefaultSamplerState()
{
    if (g_defaultSampler)
    {
        return g_defaultSampler;
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    // UV가 [0~1] 범위를 벗어는 경우 처리
    // VK_SAMPLER_ADDRESS_MODE_REPEAT : 반복해서 출력, UV % 1
    // VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : 반복하지만 거울에 비치듯 반대로 출력
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : 범위 밖은 가장자리의 색으로 모두 출력
    // VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : 범위 밖은 반대편 가장자리의 색으로 모두 출력
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : 단색으로 설정함. (samplerInfo.borderColor)
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    samplerInfo.anisotropyEnable = false;
    samplerInfo.maxAnisotropy = 1;

    // 이게 true 이면 UV 좌표가 [0, texWidth], [0, texHeight] 가 됨. false 이면 [0, 1] 범위
    samplerInfo.unnormalizedCoordinates = false;

    // compareEnable이 ture 이면, 텍셀을 특정 값과 비교한 뒤 그 결과를 필터링 연산에 사용한다.
    // Percentage-closer filtering(PCF) 에 주로 사용됨.
    samplerInfo.compareEnable = false;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    const uint32 MipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(SCR_WIDTH, SCR_HEIGHT)))) + 1;		// 이것도 수정 필요. SamplerState 는 텍스쳐에 바인딩 해야 할듯 

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;	// Optional
    samplerInfo.minLod = 0.0f;		// Optional
    samplerInfo.maxLod = static_cast<float>(MipLevels);

    if (!ensure(vkCreateSampler(g_rhi_vk->Device, &samplerInfo, nullptr, &g_defaultSampler) == VK_SUCCESS))
        return nullptr;

    return g_defaultSampler;
}

void jTexture_Vulkan::DestroyDefaultSamplerState()
{
    if (g_defaultSampler)
    {
        vkDestroySampler(g_rhi_vk->Device, g_defaultSampler, nullptr);
        g_defaultSampler = nullptr;
    }
}

