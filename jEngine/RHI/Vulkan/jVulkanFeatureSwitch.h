﻿#pragma once

#if _DEBUG
#define ENABLE_VALIDATION_LAYER 1
#else
#define ENABLE_VALIDATION_LAYER 0
#endif

#define USE_VARIABLE_SHADING_RATE_TIER2 0
#define VULKAN_NDC_Y_FLIP 1     // Make it NDC space coordinates equal to OpenGL and DirectX which are using Left hand NDC coordinates