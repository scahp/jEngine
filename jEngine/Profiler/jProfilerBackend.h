#pragma once

#include "CoreDefines.h"
#include <cstring>
#include <memory>

// External CPU profiler backend wrappers.
// GPU profiling remains on the legacy in-engine path for now.

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY
	#ifndef TRACY_ENABLE
		#define TRACY_ENABLE 1
	#endif
	#if __has_include("tracy/Tracy.hpp")
		#include "tracy/Tracy.hpp"
		#define JPROFILE_EXTERNAL_CPU_AVAILABLE 1
	#else
		#define JPROFILE_EXTERNAL_CPU_AVAILABLE 0
	#endif
#else
	#define JPROFILE_EXTERNAL_CPU_AVAILABLE 0
#endif

#if (JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY) && !JPROFILE_EXTERNAL_CPU_AVAILABLE
	#error "Selected profiler backend header is missing. Add the dependency include path or switch JPROFILE_BACKEND."
#endif

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
	#define JPROFILE_EXTERNAL_CPU_SCOPE_LITERAL(NameLiteral) ZoneScopedN(NameLiteral)
	#define JPROFILE_EXTERNAL_CPU_SCOPE_DYNAMIC_CSTR(NameCStr) \
		ZoneScoped; \
		ZoneName((NameCStr), (uint16)std::strlen(NameCStr))
#else
	#define JPROFILE_EXTERNAL_CPU_SCOPE_LITERAL(NameLiteral)
	#define JPROFILE_EXTERNAL_CPU_SCOPE_DYNAMIC_CSTR(NameCStr)
#endif

#define JPROFILE_USE_EXTERNAL_CPU (JPROFILE_EXTERNAL_CPU_AVAILABLE && (JPROFILE_BACKEND != JPROFILE_BACKEND_LEGACY))

struct jRenderFrameContext;
struct jName;

struct jProfileRuntimeOptions
{
	// Tracy recommended baseline
	bool EnableAppInfo = true;
	bool EnableMessages = true;
	bool EnablePlots = true;

	// Optional extensions (default off until explicit implementation/use)
	bool EnableFrameImage = true;
	bool EnableMemoryTracking = true;
	int32 FrameImageCaptureInterval = 15;
	int32 FrameImageCaptureMaxWidth = 320;
	int32 FrameImageSourceFrameLag = 2;
	bool FrameImageSkipIfSourceNotReady = true;
};

extern jProfileRuntimeOptions g_jProfileRuntimeOptions;

void jProfileGPUInitializeForRHI();
void jProfileGPUShutdownForRHI();
void jProfileGPUBeginFrame(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr);
void jProfileGPUEndFrame(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr);
void* jProfileGPUZoneBegin(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const char* InName);
void jProfileGPUZoneEnd(void* InZoneHandle);
void jProfileMessage(const char* InMessage);
void jProfileFrameMark();
