#include "common.hlsl"
// Scene feature shader injection point.

// The runtime loader resolves every include from this entry's directory and
// expands includes once before preprocessing. Keep shared pure helpers outside
// conditional branches so every selected pass can see them after expansion.
#include "Features/environmentlighting.hlsl"
#include "Features/pixelprojectedreflection.hlsl"
#include "Features/atmosphere.hlsl"

#if JE_SCENE_FEATURE_PASS == 1
#include "Features/scene_environment_cs.hlsl"
#elif JE_SCENE_FEATURE_PASS == 2
#include "Features/ppr_clear_cs.hlsl"
#elif JE_SCENE_FEATURE_PASS == 3
#include "Features/ppr_projection_cs.hlsl"
#elif JE_SCENE_FEATURE_PASS == 4
#include "Features/ppr_resolve_cs.hlsl"
#elif JE_SCENE_FEATURE_PASS == 5
#include "Features/scene_atmosphere_cs.hlsl"
#else
#error JE_SCENE_FEATURE_PASS must select a scene feature entry.
#endif
