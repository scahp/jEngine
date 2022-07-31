#include "pch.h"
#include "jImage_Vulkan.h"
#include "../jRHI_Vulkan.h"

void jImage_Vulkan::Destroy()
{
    vkDestroyImage(g_rhi_vk->Device, Image, nullptr);
    vkDestroyImageView(g_rhi_vk->Device, ImageView, nullptr);
    vkFreeMemory(g_rhi_vk->Device, ImageMemory, nullptr);
}
