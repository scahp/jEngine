#include "pch.h"
#include "jSwapchain_Vulkan.h"
#include "jVulkanDeviceUtil.h"
#include "jRHIType_Vulkan.h"
#include "jVulkanBufferUtil.h"

//////////////////////////////////////////////////////////////////////////
// jSwapchainImage_Vulkan
//////////////////////////////////////////////////////////////////////////
void jSwapchainImage_Vulkan::Release()
{
    ReleaseInternal();
}

void jSwapchainImage_Vulkan::ReleaseInternal()
{
    TexturePtr = nullptr;
    if (Available)
    {
        g_rhi->GetSemaphoreManager()->ReturnSemaphore(Available);
        Available = nullptr;
    }
    if (RenderFinished)
    {
        g_rhi->GetSemaphoreManager()->ReturnSemaphore(RenderFinished);
        RenderFinished = nullptr;
    }
    if (RenderFinishedAfterShadow)
    {
        g_rhi->GetSemaphoreManager()->ReturnSemaphore(RenderFinishedAfterShadow);
        RenderFinishedAfterShadow = nullptr;
    }
}

//////////////////////////////////////////////////////////////////////////
// jSwapchain_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jSwapchain_Vulkan::Create()
{
    jVulkanDeviceUtil::SwapChainSupportDetails swapChainSupport = jVulkanDeviceUtil::QuerySwapChainSupport(g_rhi_vk->PhysicalDevice, g_rhi_vk->Surface);

    VkSurfaceFormatKHR surfaceFormat = jVulkanDeviceUtil::ChooseSwapSurfaceFormat(swapChainSupport.Formats);
    VkPresentModeKHR presentMode = jVulkanDeviceUtil::ChooseSwapPresentMode(swapChainSupport.PresentModes);
    VkExtent2D extent = jVulkanDeviceUtil::ChooseSwapExtent(g_rhi_vk->Window, swapChainSupport.Capabilities);

    // SwapChain 개수 설정
    // 최소개수로 하게 되면, 우리가 렌더링할 새로운 이미지를 얻기 위해 드라이버가 내부 기능 수행을 기다려야 할 수 있으므로 min + 1로 설정.
    uint32 imageCount = swapChainSupport.Capabilities.minImageCount + 1;

    // maxImageCount가 0이면 최대 개수에 제한이 없음
    if ((swapChainSupport.Capabilities.maxImageCount > 0) && (imageCount > swapChainSupport.Capabilities.maxImageCount))
        imageCount = swapChainSupport.Capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = g_rhi_vk->Surface;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.minImageCount = imageCount;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;			// Stereoscopic 3D application(VR)이 아니면 항상 1
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT     // 즉시 스왑체인에 그리기 위해서 이걸로 설정
                            | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_STORAGE_BIT;	
                                                                    // 포스트 프로세스 같은 처리를 위해 별도의 이미지를 만드는 것이면
                                                                    // VK_IMAGE_USAGE_TRANSFER_DST_BIT 으로 하면됨.

    jVulkanDeviceUtil::QueueFamilyIndices indices = jVulkanDeviceUtil::FindQueueFamilies(g_rhi_vk->PhysicalDevice, g_rhi_vk->Surface);
    uint32 queueFamilyIndices[] = { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

    // 그림은 Graphics Queue Family와 Present Queue Family가 다른경우 아래와 같이 동작한다.
    // - 이미지를 Graphics Queue에서 가져온 스왑체인에 그리고, Presentation Queue에 제출
    // 1. VK_SHARING_MODE_EXCLUSIVE : 이미지를 한번에 하나의 Queue Family 가 소유함. 소유권이 다른곳으로 전달될때 명시적으로 전달 해줘야함.
    //								이 옵션은 최고의 성능을 제공한다.
    // 2. VK_SHARING_MODE_CONCURRENT : 이미지가 여러개의 Queue Family 로 부터 명시적인 소유권 절달 없이 사용될 수 있다.
    if (indices.GraphicsFamily != indices.PresentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;		// Optional
        createInfo.pQueueFamilyIndices = nullptr;	// Optional
    }

    createInfo.preTransform = swapChainSupport.Capabilities.currentTransform;		// 스왑체인에 회전이나 flip 처리 할 수 있음.
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;					// 알파채널을 윈도우 시스템의 다른 윈도우와 블랜딩하는데 사용해야하는지 여부
                                                                                    // VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 는 알파채널 무시
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;			// 다른윈도우에 가려져서 보이지 않는 부분을 그리지 않을지에 대한 여부 VK_TRUE 면 안그림

    // 화면 크기 변경등으로 스왑체인이 다시 만들어져야 하는경우 여기에 oldSwapchain을 넘겨준다.
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (!ensure(vkCreateSwapchainKHR(g_rhi_vk->Device, &createInfo, nullptr, &Swapchain) == VK_SUCCESS))
        return false;

    vkGetSwapchainImagesKHR(g_rhi_vk->Device, Swapchain, &imageCount, nullptr);

    std::vector<VkImage> vkImages;
    vkImages.resize(imageCount);
    vkGetSwapchainImagesKHR(g_rhi_vk->Device, Swapchain, &imageCount, vkImages.data());

    Format = GetVulkanTextureFormat(surfaceFormat.format);
    Extent = Vector2i(extent.width, extent.height);

    // ImageView 생성
    Images.resize(vkImages.size());
    for (int32 i = 0; i < Images.size(); ++i)
    {
        jSwapchainImage_Vulkan* SwapchainImage = new jSwapchainImage_Vulkan();
        Images[i] = SwapchainImage;

        auto ImagetView = jVulkanBufferUtil::CreateImageView(vkImages[i], surfaceFormat.format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        SwapchainImage->TexturePtr = std::shared_ptr<jTexture_Vulkan>(
            new jTexture_Vulkan(ETextureType::TEXTURE_2D, Format, Extent.x, Extent.y, 1, EMSAASamples::COUNT_1, 1, false, vkImages[i], ImagetView));
        SwapchainImage->CommandBufferFence = nullptr;

        SwapchainImage->Available = g_rhi->GetSemaphoreManager()->GetOrCreateSemaphore();
        SwapchainImage->RenderFinished = g_rhi->GetSemaphoreManager()->GetOrCreateSemaphore();
        SwapchainImage->RenderFinishedAfterShadow = g_rhi->GetSemaphoreManager()->GetOrCreateSemaphore();
    }

    return true;
}

void jSwapchain_Vulkan::Release()
{
    ReleaseInternal();

}

void jSwapchain_Vulkan::ReleaseInternal()
{
    vkDestroySwapchainKHR(g_rhi_vk->Device, Swapchain, nullptr);

    for (auto& iter : Images)
    {
        delete iter;
    }
}
