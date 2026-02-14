#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "pch.h"
#include "Profiler/jProfilerBackend.h"
#include "RHI/DX12/jTexture_DX12.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "RHI/Vulkan/jBuffer_Vulkan.h"
#include <new>
#include <malloc.h>
#if defined(_WIN32)
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>
#endif

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY
// Tracy is distributed as headers + a single translation unit.
// Disable Tracy system tracing (ETW) to avoid /ZI constexpr issues on this project setup.
#ifndef TRACY_NO_SYSTEM_TRACING
#define TRACY_NO_SYSTEM_TRACING
#endif

#if __has_include("TracyClient.cpp")
#include "TracyClient.cpp"
#else
#error "Tracy backend selected, but TracyClient.cpp was not found."
#endif
#endif

// Optick integration point.
#if JPROFILE_BACKEND == JPROFILE_BACKEND_OPTICK
#if __has_include("optick_core.cpp")
#include "optick_core.cpp"
#include "optick_message.cpp"
#include "optick_serialization.cpp"
#include "optick_server.cpp"
#include "optick_gpu.cpp"
#include "optick_gpu.d3d12.cpp"
#include "optick_gpu.vulkan.cpp"
#include "optick_miniz.cpp"
#include "optick_capi.cpp"
#else
#error "Optick backend selected, but Optick source files were not found."
#endif
#endif

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY
	#if __has_include("tracy/TracyD3D12.hpp")
		#include "tracy/TracyD3D12.hpp"
		#define JPROFILE_TRACY_GPU_DX12_AVAILABLE 1
	#else
		#define JPROFILE_TRACY_GPU_DX12_AVAILABLE 0
	#endif

	#if __has_include("tracy/TracyVulkan.hpp")
		#include "tracy/TracyVulkan.hpp"
		#define JPROFILE_TRACY_GPU_VULKAN_AVAILABLE 1
	#else
		#define JPROFILE_TRACY_GPU_VULKAN_AVAILABLE 0
	#endif
#else
	#define JPROFILE_TRACY_GPU_DX12_AVAILABLE 0
	#define JPROFILE_TRACY_GPU_VULKAN_AVAILABLE 0
#endif

namespace
{
	std::atomic<bool> g_MemoryTrackingLatchedInitialized = false;
	std::atomic<bool> g_MemoryTrackingLatchedValue = false;

	void EnsureMemoryTrackingLatched()
	{
		bool expected = false;
		if (g_MemoryTrackingLatchedInitialized.compare_exchange_strong(expected, true))
		{
			g_MemoryTrackingLatchedValue.store(g_jProfileRuntimeOptions.EnableMemoryTracking);
		}
	}

#if JPROFILE_TRACY_GPU_DX12_AVAILABLE
	TracyD3D12Ctx g_TracyD3D12Ctx[(int32)ECommandBufferType::MAX] = { nullptr, };
#endif

#if JPROFILE_TRACY_GPU_VULKAN_AVAILABLE
	TracyVkCtx g_TracyVkCtx[(int32)ECommandBufferType::MAX] = { nullptr, };
#endif

	HWND GetMainWindowHandle()
	{
		if (!g_rhi)
			return nullptr;

		if (IsUseDX12())
			return (HWND)g_rhi->GetWindow();

#if defined(_WIN32)
		if (IsUseVulkan())
		{
			GLFWwindow* window = (GLFWwindow*)g_rhi->GetWindow();
			if (!window)
				return nullptr;
			return glfwGetWin32Window(window);
		}
#endif
		return nullptr;
	}

	int32 AlignDownTo4(int32 InValue)
	{
		return InValue & ~3;
	}

	uint32 AlignUpTo(uint32 InValue, uint32 InAlignment)
	{
		check(InAlignment > 0);
		return (InValue + InAlignment - 1) & ~(InAlignment - 1);
	}

	bool TryCaptureFrameImageFromDX12Swapchain(std::vector<uint8>& OutRgba, int32& OutWidth, int32& OutHeight)
	{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (!IsUseDX12() || !g_rhi)
			return false;

		auto* rhiDX12 = static_cast<jRHI_DX12*>(g_rhi);
		if (!rhiDX12 || !rhiDX12->Swapchain || !rhiDX12->Device)
			return false;

		const uint32 numSwapchainImages = (uint32)rhiDX12->Swapchain->Images.size();
		if (numSwapchainImages == 0)
			return false;

		const uint32 currentFrameIndex = rhiDX12->CurrentFrameIndex % numSwapchainImages;
		const uint32 requestedLag = (uint32)std::max(0, g_jProfileRuntimeOptions.FrameImageSourceFrameLag);
		uint32 effectiveLag = requestedLag;
		if (numSwapchainImages > 1)
		{
			effectiveLag = requestedLag % numSwapchainImages;
			if (requestedLag > 0 && effectiveLag == 0)
			{
				// On triple buffering, lag=3 means same backbuffer index. Use oldest distinct backbuffer.
				effectiveLag = numSwapchainImages - 1;
			}
		}

		auto GetLaggedIndex = [&](uint32 InLag) -> uint32
		{
			return (currentFrameIndex + numSwapchainImages - (InLag % numSwapchainImages)) % numSwapchainImages;
		};

		uint32 selectedIndex = GetLaggedIndex(effectiveLag);
		auto* swapchainImage = rhiDX12->Swapchain->Images[selectedIndex];
		if (!swapchainImage || !swapchainImage->TexturePtr)
			return false;

		if (g_jProfileRuntimeOptions.FrameImageSkipIfSourceNotReady)
		{
			const auto IsComplete = [&](const jSwapchainImage_DX12* InImage)
			{
				return InImage && rhiDX12->GraphicsCommandBufferManager->Fence->IsComplete(InImage->FenceValue);
			};

			if (!IsComplete(swapchainImage))
			{
				bool foundCompletedSource = false;
				for (uint32 extraLag = 1; extraLag < numSwapchainImages; ++extraLag)
				{
					const uint32 candidateLag = effectiveLag + extraLag;
					const uint32 candidateIndex = GetLaggedIndex(candidateLag);
					auto* candidate = rhiDX12->Swapchain->Images[candidateIndex];
					if (candidate && candidate->TexturePtr && IsComplete(candidate))
					{
						swapchainImage = candidate;
						selectedIndex = candidateIndex;
						foundCompletedSource = true;
						break;
					}
				}

				if (!foundCompletedSource)
					return false;
			}
		}

		auto* textureDX12 = static_cast<jTexture_DX12*>(swapchainImage->TexturePtr.get());
		if (!textureDX12)
			return false;

		ID3D12Resource* srcResource = static_cast<ID3D12Resource*>(textureDX12->GetHandle());
		if (!srcResource)
			return false;

		const uint32 srcW = (textureDX12->Width > 0) ? (uint32)textureDX12->Width : 0u;
		const uint32 srcH = (textureDX12->Height > 0) ? (uint32)textureDX12->Height : 0u;
		if (srcW == 0 || srcH == 0)
			return false;

		const uint32 srcRowPitch = AlignUpTo(srcW * 4u, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		const uint64 readbackSize = (uint64)srcRowPitch * (uint64)srcH;
		if (readbackSize == 0)
			return false;

		static ComPtr<ID3D12Resource> sReadback;
		static uint64 sReadbackSize = 0;
		if (!sReadback || sReadbackSize < readbackSize)
		{
			sReadback.Reset();
			sReadbackSize = 0;

			const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
			const CD3DX12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(readbackSize);
			if (FAILED(rhiDX12->Device->CreateCommittedResource(
				&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&sReadback))))
			{
				return false;
			}
			sReadbackSize = readbackSize;
		}

		jCommandBuffer_DX12* cmd = rhiDX12->BeginSingleTimeCommands();
		if (!cmd || !cmd->Get())
			return false;

		rhiDX12->TransitionLayout(cmd, textureDX12, EResourceLayout::TRANSFER_SRC);
		cmd->FlushBarrierBatch();

		jTextureCopyRegion region;
		region.X = 0;
		region.Y = 0;
		region.Width = srcW;
		region.Height = srcH;
		region.ArraySlice = 0;
		region.MipLevel = 0;
		region.DestRowPitchOverride = srcRowPitch;

		jBuffer_DX12 dstBuffer;
		dstBuffer.Buffer = jCreatedResource::CreatedFromStandalone(sReadback, EResourceLayout::TRANSFER_DST, jNameStatic("TracyFrameImageReadback"));
		rhiDX12->CopyTextureRegionToBuffer(cmd, textureDX12, region, &dstBuffer, 0);

		rhiDX12->TransitionLayout(cmd, textureDX12, EResourceLayout::PRESENT_SRC);
		cmd->FlushBarrierBatch();
		rhiDX12->EndSingleTimeCommands(cmd);

		D3D12_RANGE readRange = { 0, (SIZE_T)readbackSize };
		void* mapped = nullptr;
		if (FAILED(sReadback->Map(0, &readRange, &mapped)) || !mapped)
			return false;

		const int32 maxWidth = std::max(64, g_jProfileRuntimeOptions.FrameImageCaptureMaxWidth);
		int32 dstW = std::min((int32)srcW, maxWidth);
		int32 dstH = (int32)((double)srcH * (double)dstW / (double)srcW);
		dstW = AlignDownTo4(dstW);
		dstH = AlignDownTo4(dstH);
		if (dstW < 4 || dstH < 4)
		{
			sReadback->Unmap(0, nullptr);
			return false;
		}

		OutWidth = dstW;
		OutHeight = dstH;
		OutRgba.resize((size_t)dstW * (size_t)dstH * 4);

		const uint8* srcBytes = static_cast<const uint8*>(mapped);
		for (int32 y = 0; y < dstH; ++y)
		{
			const uint32 srcY = (uint32)(((uint64)y * (uint64)srcH) / (uint64)dstH);
			const uint8* srcRow = srcBytes + (size_t)srcY * srcRowPitch;
			for (int32 x = 0; x < dstW; ++x)
			{
				const uint32 srcX = (uint32)(((uint64)x * (uint64)srcW) / (uint64)dstW);
				const uint8* srcPixel = srcRow + (size_t)srcX * 4;
				uint8* dstPixel = &OutRgba[((size_t)y * (size_t)dstW + (size_t)x) * 4];
				dstPixel[0] = srcPixel[0];
				dstPixel[1] = srcPixel[1];
				dstPixel[2] = srcPixel[2];
				dstPixel[3] = 255;
			}
		}

		sReadback->Unmap(0, nullptr);
		return true;
#else
		(void)OutRgba;
		(void)OutWidth;
		(void)OutHeight;
		return false;
#endif
	}

	bool TryCaptureFrameImageFromVulkanSwapchain(std::vector<uint8>& OutRgba, int32& OutWidth, int32& OutHeight)
	{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (!IsUseVulkan() || !g_rhi)
			return false;

		auto* rhiVulkan = static_cast<jRHI_Vulkan*>(g_rhi);
		if (!rhiVulkan || !rhiVulkan->Swapchain || !rhiVulkan->Device)
			return false;

		const uint32 numSwapchainImages = (uint32)rhiVulkan->Swapchain->Images.size();
		if (numSwapchainImages == 0)
			return false;

		const uint32 currentFrameIndex = rhiVulkan->CurrentFrameIndex % numSwapchainImages;
		const uint32 requestedLag = (uint32)std::max(0, g_jProfileRuntimeOptions.FrameImageSourceFrameLag);
		uint32 effectiveLag = requestedLag;
		if (numSwapchainImages > 1)
		{
			effectiveLag = requestedLag % numSwapchainImages;
			if (requestedLag > 0 && effectiveLag == 0)
				effectiveLag = numSwapchainImages - 1;
		}

		auto GetLaggedIndex = [&](uint32 InLag) -> uint32
		{
			return (currentFrameIndex + numSwapchainImages - (InLag % numSwapchainImages)) % numSwapchainImages;
		};

		auto IsImageReady = [&](const jSwapchainImage_Vulkan* InImage) -> bool
		{
			if (!InImage || InImage->CommandBufferFence == VK_NULL_HANDLE)
				return false;
			return (vkGetFenceStatus(rhiVulkan->Device, InImage->CommandBufferFence) == VK_SUCCESS);
		};

		uint32 selectedIndex = GetLaggedIndex(effectiveLag);
		auto* swapchainImage = rhiVulkan->Swapchain->Images[selectedIndex];
		if (!swapchainImage || !swapchainImage->TexturePtr)
			return false;

		if (g_jProfileRuntimeOptions.FrameImageSkipIfSourceNotReady)
		{
			if (!IsImageReady(swapchainImage))
			{
				bool foundCompletedSource = false;
				for (uint32 extraLag = 1; extraLag < numSwapchainImages; ++extraLag)
				{
					const uint32 candidateLag = effectiveLag + extraLag;
					const uint32 candidateIndex = GetLaggedIndex(candidateLag);
					auto* candidate = rhiVulkan->Swapchain->Images[candidateIndex];
					if (candidate && candidate->TexturePtr && IsImageReady(candidate))
					{
						swapchainImage = candidate;
						selectedIndex = candidateIndex;
						foundCompletedSource = true;
						break;
					}
				}

				if (!foundCompletedSource)
					return false;
			}
		}

		auto* textureVulkan = static_cast<jTexture_Vulkan*>(swapchainImage->TexturePtr.get());
		if (!textureVulkan)
			return false;

		const uint32 srcW = (textureVulkan->Width > 0) ? (uint32)textureVulkan->Width : 0u;
		const uint32 srcH = (textureVulkan->Height > 0) ? (uint32)textureVulkan->Height : 0u;
		if (srcW == 0 || srcH == 0)
			return false;

		const uint32 srcRowPitch = srcW * 4u;
		const uint64 readbackSize = (uint64)srcRowPitch * (uint64)srcH;
		if (readbackSize == 0)
			return false;

		static std::shared_ptr<jBuffer_Vulkan> sReadback;
		static uint64 sReadbackSize = 0;
		if (!sReadback || sReadbackSize < readbackSize)
		{
			sReadback.reset();
			sReadbackSize = 0;

			sReadback = std::static_pointer_cast<jBuffer_Vulkan>(
				g_rhi->CreateRawBuffer(readbackSize, 1, EBufferCreateFlag::CPUAccess, EResourceLayout::TRANSFER_DST));
			if (!sReadback)
				return false;
			sReadbackSize = readbackSize;
		}

		jCommandBuffer_Vulkan* cmd = rhiVulkan->BeginSingleTimeCommands();
		if (!cmd)
			return false;

		rhiVulkan->TransitionLayout(cmd, textureVulkan, EResourceLayout::TRANSFER_SRC);
		cmd->FlushBarrierBatch();

		jTextureCopyRegion region;
		region.X = 0;
		region.Y = 0;
		region.Width = srcW;
		region.Height = srcH;
		region.ArraySlice = 0;
		region.MipLevel = 0;
		region.DestRowPitchOverride = srcRowPitch;
		rhiVulkan->CopyTextureRegionToBuffer(cmd, textureVulkan, region, sReadback.get(), 0);

		rhiVulkan->TransitionLayout(cmd, textureVulkan, EResourceLayout::PRESENT_SRC);
		cmd->FlushBarrierBatch();
		rhiVulkan->EndSingleTimeCommands(cmd);

		const uint8* srcBytes = static_cast<const uint8*>(sReadback->Map());
		if (!srcBytes)
			return false;

		const int32 maxWidth = std::max(64, g_jProfileRuntimeOptions.FrameImageCaptureMaxWidth);
		int32 dstW = std::min((int32)srcW, maxWidth);
		int32 dstH = (int32)((double)srcH * (double)dstW / (double)srcW);
		dstW = AlignDownTo4(dstW);
		dstH = AlignDownTo4(dstH);
		if (dstW < 4 || dstH < 4)
		{
			sReadback->Unmap();
			return false;
		}

		OutWidth = dstW;
		OutHeight = dstH;
		OutRgba.resize((size_t)dstW * (size_t)dstH * 4);

		for (int32 y = 0; y < dstH; ++y)
		{
			const uint32 srcY = (uint32)(((uint64)y * (uint64)srcH) / (uint64)dstH);
			const uint8* srcRow = srcBytes + (size_t)srcY * srcRowPitch;
			for (int32 x = 0; x < dstW; ++x)
			{
				const uint32 srcX = (uint32)(((uint64)x * (uint64)srcW) / (uint64)dstW);
				const uint8* srcPixel = srcRow + (size_t)srcX * 4;
				uint8* dstPixel = &OutRgba[((size_t)y * (size_t)dstW + (size_t)x) * 4];
				dstPixel[0] = srcPixel[0];
				dstPixel[1] = srcPixel[1];
				dstPixel[2] = srcPixel[2];
				dstPixel[3] = 255;
			}
		}

		sReadback->Unmap();
		return true;
#else
		(void)OutRgba;
		(void)OutWidth;
		(void)OutHeight;
		return false;
#endif
	}

	void TrySendFrameImage()
	{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (!g_jProfileRuntimeOptions.EnableFrameImage)
			return;
		if (!TracyIsConnected)
			return;
		static uint32 LastCapturedFrameNumber = std::numeric_limits<uint32>::max();
		const uint32 CurrentFrameNumber = g_rhi->GetCurrentFrameNumber();
		if (LastCapturedFrameNumber == CurrentFrameNumber)
			return;

		const int32 Interval = std::max(1, g_jProfileRuntimeOptions.FrameImageCaptureInterval);
		if ((CurrentFrameNumber % (uint32)Interval) != 0)
			return;
		LastCapturedFrameNumber = CurrentFrameNumber;

		static std::vector<uint8> rgba;
		int32 imageW = 0;
		int32 imageH = 0;
		if (TryCaptureFrameImageFromDX12Swapchain(rgba, imageW, imageH))
		{
			FrameImage(rgba.data(), (uint16)imageW, (uint16)imageH, 0, false);
			return;
		}
		if (TryCaptureFrameImageFromVulkanSwapchain(rgba, imageW, imageH))
		{
			FrameImage(rgba.data(), (uint16)imageW, (uint16)imageH, 0, false);
			return;
		}

		HWND hwnd = GetMainWindowHandle();
		if (!hwnd)
			return;

		RECT rc = {};
		if (!GetClientRect(hwnd, &rc))
			return;
		const int32 srcW = rc.right - rc.left;
		const int32 srcH = rc.bottom - rc.top;
		if (srcW <= 0 || srcH <= 0)
			return;

		const int32 maxWidth = std::max(64, g_jProfileRuntimeOptions.FrameImageCaptureMaxWidth);
		int32 dstW = std::min(srcW, maxWidth);
		int32 dstH = (int32)((double)srcH * (double)dstW / (double)srcW);
		dstW = AlignDownTo4(dstW);
		dstH = AlignDownTo4(dstH);
		if (dstW < 4 || dstH < 4)
			return;

		HDC srcDC = GetDC(hwnd);
		if (!srcDC)
			return;

		HDC memDC = CreateCompatibleDC(srcDC);
		if (!memDC)
		{
			ReleaseDC(hwnd, srcDC);
			return;
		}

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = dstW;
		bmi.bmiHeader.biHeight = -dstH; // Top-down bitmap
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void* dibBits = nullptr;
		HBITMAP dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
		if (!dib || !dibBits)
		{
			if (dib)
				DeleteObject(dib);
			DeleteDC(memDC);
			ReleaseDC(hwnd, srcDC);
			return;
		}

		HGDIOBJ oldObj = SelectObject(memDC, dib);
		SetStretchBltMode(memDC, HALFTONE);
		StretchBlt(memDC, 0, 0, dstW, dstH, srcDC, 0, 0, srcW, srcH, SRCCOPY);

		rgba.resize((size_t)dstW * (size_t)dstH * 4);

		const uint8* bgra = (const uint8*)dibBits;
		for (int32 i = 0, pixelCount = dstW * dstH; i < pixelCount; ++i)
		{
			rgba[(size_t)i * 4 + 0] = bgra[(size_t)i * 4 + 2];
			rgba[(size_t)i * 4 + 1] = bgra[(size_t)i * 4 + 1];
			rgba[(size_t)i * 4 + 2] = bgra[(size_t)i * 4 + 0];
			rgba[(size_t)i * 4 + 3] = 255;
		}

		FrameImage(rgba.data(), (uint16)dstW, (uint16)dstH, 0, false);

		SelectObject(memDC, oldObj);
		DeleteObject(dib);
		DeleteDC(memDC);
		ReleaseDC(hwnd, srcDC);
#endif
	}

	FORCEINLINE bool ShouldTrackMemory()
	{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		EnsureMemoryTrackingLatched();
		return g_MemoryTrackingLatchedValue.load();
#else
		return false;
#endif
	}

	FORCEINLINE void TrackMemoryAlloc(void* InPtr, size_t InSize)
	{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (!InPtr || !ShouldTrackMemory())
			return;

		static thread_local bool IsReentrant = false;
		if (IsReentrant)
			return;

		IsReentrant = true;
		TracyAlloc(InPtr, InSize);
		IsReentrant = false;
#else
		(void)InPtr;
		(void)InSize;
#endif
	}

	FORCEINLINE void TrackMemoryFree(void* InPtr)
	{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (!InPtr || !ShouldTrackMemory())
			return;

		static thread_local bool IsReentrant = false;
		if (IsReentrant)
			return;

		IsReentrant = true;
		TracyFree(InPtr);
		IsReentrant = false;
#else
		(void)InPtr;
#endif
	}
}

jProfileRuntimeOptions g_jProfileRuntimeOptions;

void jProfileFrameMark()
{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
	FrameMark;
#endif
}

void jProfileMessage(const char* InMessage)
{
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
	if (g_jProfileRuntimeOptions.EnableMessages && InMessage)
	{
		TracyMessageL(InMessage);
	}
#else
	(void)InMessage;
#endif
}

void jProfileGPUInitializeForRHI()
{
	EnsureMemoryTrackingLatched();

#if JPROFILE_TRACY_GPU_DX12_AVAILABLE
	if (IsUseDX12() && g_rhi)
	{
		auto* rhiDX12 = static_cast<jRHI_DX12*>(g_rhi);
		check(rhiDX12);

		const char* ContextNames[(int32)ECommandBufferType::MAX] = { "DX12 Graphics", "DX12 Compute", "DX12 Copy" };
		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			auto* manager = static_cast<jCommandBufferManager_DX12*>(rhiDX12->GetCommandBufferManager((ECommandBufferType)i));
			if (!manager)
				continue;

			auto queue = manager->GetCommandQueue();
			if (!queue)
				continue;

			g_TracyD3D12Ctx[i] = TracyD3D12Context(rhiDX12->Device.Get(), queue.Get());
			if (g_TracyD3D12Ctx[i])
			{
				const char* contextName = ContextNames[i];
				TracyD3D12ContextName(g_TracyD3D12Ctx[i], contextName, (uint16)std::strlen(contextName));
			}
		}

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (g_jProfileRuntimeOptions.EnableAppInfo)
		{
			TracySetProgramName("jEngine");
			const char* appInfo = "RHI: DX12";
			TracyAppInfo(appInfo, (uint16)std::strlen(appInfo));
		}
#endif
		jProfileMessage("jProfile: Tracy GPU context initialized (DX12).");
		return;
	}
#endif

#if JPROFILE_TRACY_GPU_VULKAN_AVAILABLE
	if (IsUseVulkan() && g_rhi)
	{
		auto* rhiVulkan = static_cast<jRHI_Vulkan*>(g_rhi);
		check(rhiVulkan);

		const char* ContextNames[(int32)ECommandBufferType::MAX] = { "Vulkan Graphics", "Vulkan Compute", "Vulkan Copy" };
		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			auto* manager = static_cast<jCommandBufferManager_Vulkan*>(rhiVulkan->GetCommandBufferManager((ECommandBufferType)i));
			if (!manager)
				continue;

			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = manager->GetPool();
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer tempCommandBuffer = VK_NULL_HANDLE;
			if (vkAllocateCommandBuffers(rhiVulkan->Device, &allocInfo, &tempCommandBuffer) != VK_SUCCESS || tempCommandBuffer == VK_NULL_HANDLE)
				continue;

			const VkQueue queue = rhiVulkan->GetQueue((ECommandBufferType)i).Queue;
			g_TracyVkCtx[i] = TracyVkContext(rhiVulkan->PhysicalDevice, rhiVulkan->Device, queue, tempCommandBuffer);

			vkFreeCommandBuffers(rhiVulkan->Device, manager->GetPool(), 1, &tempCommandBuffer);

			if (g_TracyVkCtx[i])
			{
				const char* contextName = ContextNames[i];
				TracyVkContextName(g_TracyVkCtx[i], contextName, (uint16)std::strlen(contextName));
			}
		}

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (g_jProfileRuntimeOptions.EnableAppInfo)
		{
			TracySetProgramName("jEngine");
			const char* appInfo = "RHI: Vulkan";
			TracyAppInfo(appInfo, (uint16)std::strlen(appInfo));
		}
#endif
		jProfileMessage("jProfile: Tracy GPU context initialized (Vulkan).");
		return;
	}
#endif
}

void jProfileGPUShutdownForRHI()
{
#if JPROFILE_TRACY_GPU_DX12_AVAILABLE
	for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
	{
		if (g_TracyD3D12Ctx[i])
		{
			TracyD3D12Destroy(g_TracyD3D12Ctx[i]);
			g_TracyD3D12Ctx[i] = nullptr;
		}
	}
#endif

#if JPROFILE_TRACY_GPU_VULKAN_AVAILABLE
	for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
	{
		if (g_TracyVkCtx[i])
		{
			TracyVkDestroy(g_TracyVkCtx[i]);
			g_TracyVkCtx[i] = nullptr;
		}
	}
#endif
	jProfileMessage("jProfile: Tracy GPU context shutdown.");
}

void jProfileGPUBeginFrame(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
	(void)InRenderFrameContextPtr;

#if JPROFILE_TRACY_GPU_DX12_AVAILABLE
	if (IsUseDX12())
	{
		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			if (g_TracyD3D12Ctx[i])
				TracyD3D12NewFrame(g_TracyD3D12Ctx[i]);
		}
	}
#endif
}

void jProfileGPUEndFrame(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
#if JPROFILE_TRACY_GPU_DX12_AVAILABLE
	if (IsUseDX12())
	{
		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			if (g_TracyD3D12Ctx[i])
				TracyD3D12Collect(g_TracyD3D12Ctx[i]);
		}

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (g_jProfileRuntimeOptions.EnablePlots)
		{
			TracyPlot("Frame.DeltaMs", (double)g_timeDeltaSecond * 1000.0);
			TracyPlot("Frame.FPS", (g_timeDeltaSecond > 0.0f) ? (1.0 / (double)g_timeDeltaSecond) : 0.0);
		}
#endif
		TrySendFrameImage();
		return;
	}
#endif

#if JPROFILE_TRACY_GPU_VULKAN_AVAILABLE
	if (IsUseVulkan() && InRenderFrameContextPtr)
	{
		auto* commandBuffer = InRenderFrameContextPtr->GetActiveCommandBuffer();
		if (!commandBuffer)
			return;

		VkCommandBuffer vkCommandBuffer = (VkCommandBuffer)commandBuffer->GetHandle();
		if (!vkCommandBuffer)
			return;

		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			if (g_TracyVkCtx[i])
				TracyVkCollect(g_TracyVkCtx[i], vkCommandBuffer);
		}

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
		if (g_jProfileRuntimeOptions.EnablePlots)
		{
			TracyPlot("Frame.DeltaMs", (double)g_timeDeltaSecond * 1000.0);
			TracyPlot("Frame.FPS", (g_timeDeltaSecond > 0.0f) ? (1.0 / (double)g_timeDeltaSecond) : 0.0);
		}
#endif
		TrySendFrameImage();
	}
#endif
}

void* jProfileGPUZoneBegin(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const char* InName)
{
	if (!InRenderFrameContextPtr || !InName)
		return nullptr;

	auto* commandBuffer = InRenderFrameContextPtr->GetActiveCommandBuffer();
	if (!commandBuffer)
		return nullptr;

	const ECommandBufferType commandType = commandBuffer->Type;
	const int32 commandTypeIndex = (int32)commandType;
	if (commandTypeIndex < 0 || commandTypeIndex >= (int32)ECommandBufferType::MAX)
		return nullptr;

#if JPROFILE_TRACY_GPU_DX12_AVAILABLE
	if (IsUseDX12())
	{
		TracyD3D12Ctx context = g_TracyD3D12Ctx[commandTypeIndex];
		if (!context)
			return nullptr;

		auto* commandBufferDX12 = static_cast<jCommandBuffer_DX12*>(commandBuffer);
		check(commandBufferDX12);
		return new tracy::D3D12ZoneScope(context
			, (uint32_t)__LINE__, __FILE__, std::strlen(__FILE__), __FUNCTION__, std::strlen(__FUNCTION__)
			, InName, std::strlen(InName), commandBufferDX12->Get(), true);
	}
#endif

#if JPROFILE_TRACY_GPU_VULKAN_AVAILABLE
	if (IsUseVulkan())
	{
		TracyVkCtx context = g_TracyVkCtx[commandTypeIndex];
		if (!context)
			return nullptr;

		return new tracy::VkCtxScope(context
			, (uint32_t)__LINE__, __FILE__, std::strlen(__FILE__), __FUNCTION__, std::strlen(__FUNCTION__)
			, InName, std::strlen(InName), (VkCommandBuffer)commandBuffer->GetHandle(), true);
	}
#endif

	return nullptr;
}

void jProfileGPUZoneEnd(void* InZoneHandle)
{
	if (!InZoneHandle)
		return;

#if JPROFILE_TRACY_GPU_DX12_AVAILABLE
	if (IsUseDX12())
	{
		delete static_cast<tracy::D3D12ZoneScope*>(InZoneHandle);
		return;
	}
#endif

#if JPROFILE_TRACY_GPU_VULKAN_AVAILABLE
	if (IsUseVulkan())
	{
		delete static_cast<tracy::VkCtxScope*>(InZoneHandle);
		return;
	}
#endif
}

void* operator new(size_t InSize)
{
	if (void* p = std::malloc(InSize))
	{
		TrackMemoryAlloc(p, InSize);
		return p;
	}
	throw std::bad_alloc();
}

void operator delete(void* InPtr) noexcept
{
	TrackMemoryFree(InPtr);
	std::free(InPtr);
}

void* operator new[](size_t InSize)
{
	if (void* p = std::malloc(InSize))
	{
		TrackMemoryAlloc(p, InSize);
		return p;
	}
	throw std::bad_alloc();
}

void operator delete[](void* InPtr) noexcept
{
	TrackMemoryFree(InPtr);
	std::free(InPtr);
}

void operator delete(void* InPtr, size_t) noexcept
{
	TrackMemoryFree(InPtr);
	std::free(InPtr);
}

void operator delete[](void* InPtr, size_t) noexcept
{
	TrackMemoryFree(InPtr);
	std::free(InPtr);
}

void* operator new(size_t InSize, const std::nothrow_t&) noexcept
{
	void* p = std::malloc(InSize);
	TrackMemoryAlloc(p, InSize);
	return p;
}

void operator delete(void* InPtr, const std::nothrow_t&) noexcept
{
	TrackMemoryFree(InPtr);
	std::free(InPtr);
}

void* operator new[](size_t InSize, const std::nothrow_t&) noexcept
{
	void* p = std::malloc(InSize);
	TrackMemoryAlloc(p, InSize);
	return p;
}

void operator delete[](void* InPtr, const std::nothrow_t&) noexcept
{
	TrackMemoryFree(InPtr);
	std::free(InPtr);
}

void* operator new(size_t InSize, std::align_val_t InAlign)
{
	const size_t alignment = (size_t)InAlign;
	void* p = _aligned_malloc(InSize, alignment);
	if (!p)
		throw std::bad_alloc();
	TrackMemoryAlloc(p, InSize);
	return p;
}

void operator delete(void* InPtr, std::align_val_t) noexcept
{
	TrackMemoryFree(InPtr);
	_aligned_free(InPtr);
}

void* operator new[](size_t InSize, std::align_val_t InAlign)
{
	const size_t alignment = (size_t)InAlign;
	void* p = _aligned_malloc(InSize, alignment);
	if (!p)
		throw std::bad_alloc();
	TrackMemoryAlloc(p, InSize);
	return p;
}

void operator delete[](void* InPtr, std::align_val_t) noexcept
{
	TrackMemoryFree(InPtr);
	_aligned_free(InPtr);
}

void operator delete(void* InPtr, size_t, std::align_val_t) noexcept
{
	TrackMemoryFree(InPtr);
	_aligned_free(InPtr);
}

void operator delete[](void* InPtr, size_t, std::align_val_t) noexcept
{
	TrackMemoryFree(InPtr);
	_aligned_free(InPtr);
}
