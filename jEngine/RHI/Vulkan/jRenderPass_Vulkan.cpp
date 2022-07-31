#include "pch.h"
#include "jRenderPass_Vulkan.h"
#include "jRHIType_Vulkan.h"

std::unordered_map<size_t, VkRenderPass> s_renderPassPool;

void jRenderPass_Vulkan::Release()
{
    vkDestroyRenderPass(g_rhi_vk->Device, RenderPass, nullptr);
}

size_t jRenderPass_Vulkan::GetHash() const
{
    if (Hash)
        return Hash;

    Hash = __super::GetHash();
    Hash ^= STATIC_NAME_CITY_HASH("ClearValues");
    Hash ^= CityHash64((const char*)ClearValues.data(), sizeof(VkClearValue) * ClearValues.size());
    return Hash;
}

bool jRenderPass_Vulkan::CreateRenderPass()
{
    int32 SampleCount = 0;
    int32 LayerCount = 0;

    const auto renderPassHash = GetHash();
    auto it_find = s_renderPassPool.find(renderPassHash);
    if (s_renderPassPool.end() != it_find)
    {
        RenderPass = it_find->second;

        for (int32 i = 0; i < ColorAttachments.size(); ++i)
        {
            const jAttachment* attachment = ColorAttachments[i];
            check(attachment);
            check(attachment->RenderTargetPtr.get());

            const auto& color = attachment->ClearColor;
            VkClearValue colorClear = {};
            colorClear.color = { color.x, color.y, color.z, color.w };
            ClearValues.push_back(colorClear);

            const auto& RTInfo = attachment->RenderTargetPtr->Info;

            check(SampleCount == 0 || SampleCount == RTInfo.SampleCount);
            check(LayerCount == 0 || LayerCount == RTInfo.LayerCount);
            SampleCount = RTInfo.SampleCount;
            LayerCount = RTInfo.LayerCount;
        }

        if (DepthAttachment)
        {
            const jAttachment* attachment = DepthAttachment;
            check(attachment);
            check(attachment->RenderTargetPtr.get());

            VkClearValue colorClear = {};
            colorClear.depthStencil = { DepthAttachment->ClearDepth.x, (uint32)DepthAttachment->ClearDepth.y };
            ClearValues.push_back(colorClear);

            const auto& RTInfo = attachment->RenderTargetPtr->Info;

            if (SampleCount == 0)
                SampleCount = RTInfo.SampleCount;
            if (LayerCount == 0)
                LayerCount = RTInfo.LayerCount;
        }
    }
    else
    {
        int32 AttachmentIndex = 0;
        std::vector<VkAttachmentDescription> AttachmentDescs;

        const size_t Attachments = ColorAttachments.size() + (DepthAttachment ? 1 : 0) + (ColorAttachmentResolve ? 1 : 0);
        AttachmentDescs.resize(Attachments);

        std::vector<VkAttachmentReference> colorAttachmentRefs;
        colorAttachmentRefs.resize(ColorAttachments.size());
        for (int32 i = 0; i < ColorAttachments.size(); ++i)
        {
            const jAttachment* attachment = ColorAttachments[i];
            check(attachment);
            check(attachment->RenderTargetPtr.get());

            const auto& RTInfo = attachment->RenderTargetPtr->Info;

            VkAttachmentDescription& colorDesc = AttachmentDescs[AttachmentIndex];
            colorDesc.format = GetVulkanTextureFormat(RTInfo.Format);
            colorDesc.samples = (VkSampleCountFlagBits)RTInfo.SampleCount;
            GetVulkanAttachmentLoadStoreOp(colorDesc.loadOp, colorDesc.storeOp, attachment->LoadStoreOp);
            GetVulkanAttachmentLoadStoreOp(colorDesc.stencilLoadOp, colorDesc.stencilStoreOp, attachment->StencilLoadStoreOp);

            check((ColorAttachmentResolve && ((int32)RTInfo.SampleCount > 1)) || (!ColorAttachmentResolve && (int32)RTInfo.SampleCount == 1));
            check(SampleCount == 0 || SampleCount == RTInfo.SampleCount);
            check(LayerCount == 0 || LayerCount == RTInfo.LayerCount);
            SampleCount = RTInfo.SampleCount;
            LayerCount = RTInfo.LayerCount;

            // Texture나 Framebuffer의 경우 VkImage 객체로 특정 픽셀 형식을 표현함.
            // 그러나 메모리의 픽셀 레이아웃은 이미지로 수행하려는 작업에 따라서 변경될 수 있음.
            // 그래서 여기서는 수행할 작업에 적절한 레이아웃으로 image를 전환시켜주는 작업을 함.
            // 주로 사용하는 layout의 종류
            // 1). VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : Color attachment의 이미지
            // 2). VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : Swapchain으로 제출된 이미지
            // 3). VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : Memory copy 연산의 Destination으로 사용된 이미지
            colorDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;			// RenderPass가 시작되기 전에 어떤 Image 레이아웃을 가지고 있을지 여부를 지정
                                                                                // VK_IMAGE_LAYOUT_UNDEFINED 은 이전 이미지 레이아웃을 무시한다는 의미.
                                                                                // 주의할 점은 이미지의 내용이 보존되지 않습니다. 그러나 현재는 이미지를 Clear할 것이므로 보존할 필요가 없음.

            //colorDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;	// RenderPass가 끝날때 자동으로 전환될 Image 레이아웃을 정의함
            //																	// 우리는 렌더링 결과를 스왑체인에 제출할 것이기 때문에 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 사용

            // MSAA 를 사용하게 되면, Swapchain에 제출전에 resolve 를 해야하므로, 아래와 같이 final layout 을 변경해줌.
            // 그리고 reoslve를 위한 VkAttachmentDescription 을 추가함. depth buffer의 경우는 Swapchain에 제출하지 않기 때문에 이 과정이 필요없음.
            if (SampleCount > 1)
            {
                if (DepthAttachment->TransitToShaderReadAtFinal)
                    colorDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                else
                    colorDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            else
            {
                colorDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;			// MSAA 안하면 바로 Present 할 것이므로
            }

            //////////////////////////////////////////////////////////////////////////

            // Subpasses
            // 하나의 렌더링패스에는 여러개의 서브렌더패스가 존재할 수 있다. 예를 들어서 포스트프로세스를 처리할때 여러 포스트프로세스를 
            // 서브패스로 전달하여 하나의 렌더패스로 만들 수 있다. 이렇게 하면 불칸이 Operation과 메모리 대역폭을 아껴쓸 수도 있으므로 성능이 더 나아진다.
            // 포스프 프로세스 처리들 사이에 (GPU <-> Main memory에 계속해서 Framebuffer 내용을 올렸다 내렸다 하지 않고 계속 GPU에 올려두고 처리할 수 있음)
            // 현재는 1개의 서브패스만 사용함.
            VkAttachmentReference& colorAttachmentRef = colorAttachmentRefs[i];
            colorAttachmentRef.attachment = AttachmentIndex++;							// VkAttachmentDescription 배열의 인덱스
                                                                                        // fragment shader의 layout(location = 0) out vec4 outColor; 에 location=? 에 인덱스가 매칭됨.
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;		// 서브패스에서 어떤 이미지 레이아웃을 사용할것인지를 명세함.
                                                                                        // Vulkan에서 이 서브패스가 되면 자동으로 Image 레이아웃을 이것으로 변경함.
                                                                                        // 우리는 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 으로 설정하므로써 color attachment로 사용

            const auto& color = attachment->ClearColor;
            VkClearValue colorClear = {};
            colorClear.color = { color.x, color.y, color.z, color.w };
            ClearValues.push_back(colorClear);
        }

        if (DepthAttachment)
        {
            check(DepthAttachment->RenderTargetPtr.get());
            const auto& RTInfo = DepthAttachment->RenderTargetPtr->Info;

            VkAttachmentDescription& depthAttachment = AttachmentDescs[AttachmentIndex];
            depthAttachment.format = GetVulkanTextureFormat(RTInfo.Format);
            depthAttachment.samples = (VkSampleCountFlagBits)RTInfo.SampleCount;
            GetVulkanAttachmentLoadStoreOp(depthAttachment.loadOp, depthAttachment.storeOp, DepthAttachment->LoadStoreOp);
            GetVulkanAttachmentLoadStoreOp(depthAttachment.stencilLoadOp, depthAttachment.stencilStoreOp, DepthAttachment->StencilLoadStoreOp);
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            // 최종 쉐이더 리로스 레이아웃 결정 - 쉐이더에서 읽는 리소스로 사용할 경우 처리
            if (DepthAttachment->TransitToShaderReadAtFinal)
                depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            else
                depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            //check((ColorAttachmentResolve && ((int32)RTInfo.SampleCount > 1)) || (!ColorAttachmentResolve && (int32)RTInfo.SampleCount == 1));
            //check(SampleCount == 0 || SampleCount == RTInfo.SampleCount);
            //check(LayerCount == 0 || LayerCount == RTInfo.LayerCount);
            if (SampleCount == 0)
                SampleCount = RTInfo.SampleCount;
            if (LayerCount == 0)
                LayerCount = RTInfo.LayerCount;

            VkClearValue colorClear = {};
            colorClear.depthStencil = { DepthAttachment->ClearDepth.x, (uint32)DepthAttachment->ClearDepth.y };
            ClearValues.push_back(colorClear);
        }

        VkAttachmentReference colorAttachmentResolveRef = {};
        if (SampleCount > 1)
        {
            check(ColorAttachmentResolve);
            check(ColorAttachmentResolve->RenderTargetPtr.get());
            const auto& RTInfo = ColorAttachmentResolve->RenderTargetPtr->Info;

            VkAttachmentDescription colorAttachmentResolve = {};
            colorAttachmentResolve.format = GetVulkanTextureFormat(RTInfo.Format);
            colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
            GetVulkanAttachmentLoadStoreOp(colorAttachmentResolve.loadOp, colorAttachmentResolve.storeOp, ColorAttachmentResolve->LoadStoreOp);
            GetVulkanAttachmentLoadStoreOp(colorAttachmentResolve.stencilLoadOp, colorAttachmentResolve.stencilStoreOp, ColorAttachmentResolve->StencilLoadStoreOp);
            colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            AttachmentDescs.emplace_back(colorAttachmentResolve);

            check(RTInfo.SampleCount == 1);
            check(LayerCount == RTInfo.LayerCount);

            colorAttachmentResolveRef.attachment = AttachmentIndex++;		// color attachments + depth attachment
            colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (colorAttachmentRefs.size() > 0)
        {
            subpass.colorAttachmentCount = (uint32)colorAttachmentRefs.size();
            subpass.pColorAttachments = &colorAttachmentRefs[0];
        }

        VkAttachmentReference depthAttachmentRef = {};
        if (DepthAttachment)
        {
            depthAttachmentRef.attachment = AttachmentIndex++;
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            subpass.pDepthStencilAttachment = &depthAttachmentRef;
        }

        if (SampleCount > 1)
            subpass.pResolveAttachments = &colorAttachmentResolveRef;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(AttachmentDescs.size());
        renderPassInfo.pAttachments = AttachmentDescs.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;


        // 1. Subpasses 들의 이미지 레이아웃 전환은 자동적으로 일어나며, 이런 전환은 subpass dependencies 로 관리 됨.
        // 현재 서브패스를 1개 쓰고 있지만 서브패스 앞, 뒤에 암묵적인 서브페스가 있음.
        // 2. 2개의 내장된 dependencies 가 렌더패스 시작과 끝에 있음.
        //		- 렌더패스 시작에 있는 경우 정확한 타이밍에 발생하지 않음. 파이프라인 시작에서 전환이 발생한다고 가정되지만 우리가 아직 이미지를 얻지 못한 경우가 있을 수 있음.
        //		 이 문제를 해결하기 위해서 2가지 방법이 있음.
        //			1). imageAvailableSemaphore 의 waitStages를 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 로 변경하여, 이미지가 사용가능하기 전에 렌더패스가 시작되지 못하도록 함.
        //			2). 렌더패스가 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 스테이지를 기다리게 함. (여기선 이거 씀)
        VkSubpassDependency dependency = {};
        // VK_SUBPASS_EXTERNAL : implicit subpass 인 before or after render pass 에서 발생하는 subpass
        // 여기서 dstSubpass 는 0으로 해주는데 현재 1개의 subpass 만 만들어서 0번 index로 설정함.
        // 디펜던시 그래프에서 사이클 발생을 예방하기 위해서 dstSubpass는 항상 srcSubpass 더 높아야한다. (VK_SUBPASS_EXTERNAL 은 예외)
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;

        // 수행되길 기다려야하는 작업과 이런 작업이 수행되야할 스테이지를 명세하는 부분.
        // 우리가 이미지에 접근하기 전에 스왑체인에서 읽기가 완료될때 까지 기다려야하는데, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 에서 가능.
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;

        // 기다려야하는 작업들은 Color Attachment 스테이지에 있고 Color Attachment를 읽고 쓰는 것과 관련되어 있음.
        // 이 설정으로 실제 이미지가 필요할때 혹은 허용될때까지 전환이 발생하는것을 방지함.
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;


        //std::array<VkSubpassDependency, 2> dependencies;

        //dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        //dependencies[0].dstSubpass = 0;
        //dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        //dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        //dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        //dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        //dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        //dependencies[1].srcSubpass = 0;
        //dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        //dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        //dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        //dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        //dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        //dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        //renderPassInfo.dependencyCount = (int32)dependencies.size();
        //renderPassInfo.pDependencies = dependencies.data();

        if (!ensure(vkCreateRenderPass(g_rhi_vk->GetDevice(), &renderPassInfo, nullptr, &RenderPass) == VK_SUCCESS))
            return false;

        s_renderPassPool.insert(std::make_pair(renderPassHash, RenderPass));
    }

    // Create RenderPassInfo
    RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RenderPassInfo.renderPass = RenderPass;

    // 렌더될 영역이며, 최상의 성능을 위해 attachment의 크기와 동일해야함.
    RenderPassInfo.renderArea.offset = { RenderOffset.x, RenderOffset.y };
    RenderPassInfo.renderArea.extent = { (uint32)RenderExtent.x, (uint32)RenderExtent.y };

    RenderPassInfo.clearValueCount = static_cast<uint32_t>(ClearValues.size());
    RenderPassInfo.pClearValues = ClearValues.data();

    // Create framebuffer
    {
        std::vector<VkImageView> ImageViews;

        for (int32 k = 0; k < ColorAttachments.size(); ++k)
        {
            check(ColorAttachments[k]);

            const auto* RT = ColorAttachments[k]->RenderTargetPtr.get();
            check(RT);

            const jTexture* texture = RT->GetTexture();
            check(texture);

            ImageViews.push_back((VkImageView)texture->GetViewHandle());
        }

        if (DepthAttachment)
        {
            const auto* RT = DepthAttachment->RenderTargetPtr.get();
            check(RT);

            const jTexture* texture = RT->GetTexture();
            check(texture);

            ImageViews.push_back((VkImageView)texture->GetViewHandle());
        }

        if (SampleCount > 1)
        {
            check(ColorAttachmentResolve);

            const auto* RT = ColorAttachmentResolve->RenderTargetPtr.get();
            check(RT);

            const jTexture* texture = RT->GetTexture();
            check(texture);

            ImageViews.push_back((VkImageView)texture->GetViewHandle());
        }

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = RenderPass;

        // RenderPass와 같은 수와 같은 타입의 attachment 를 사용
        framebufferInfo.attachmentCount = static_cast<uint32>(ImageViews.size());
        framebufferInfo.pAttachments = ImageViews.data();

        framebufferInfo.width = RenderExtent.x;
        framebufferInfo.height = RenderExtent.y;
        framebufferInfo.layers = LayerCount;			// 이미지 배열의 레이어수(3D framebuffer에 사용할 듯)

        if (!ensure(vkCreateFramebuffer(g_rhi_vk->Device, &framebufferInfo, nullptr, &FrameBuffer) == VK_SUCCESS))
            return false;
    }

    return true;
}
