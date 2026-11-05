#ifndef JENGINE_PIXEL_PROJECTED_REFLECTION_HLSL
#define JENGINE_PIXEL_PROJECTED_REFLECTION_HLSL

// A dominant-axis distance key, analogous to the Shadows PPR intermediate
// representation. Atomic min chooses the nearest projected offset on overlap.
// Integer source offsets keep the sampled source pixel exact, without requiring
// 64-bit atomics. Signed minor offsets support 16K render-target dimensions.
static const uint PPR_EMPTY = 0xffffffffu;
uint PPREncodeOffset(int2 offset)
{
    const int2 absoluteOffset = abs(offset);
    if (any(absoluteOffset >= 16384)) return PPR_EMPTY;
    const bool dominantX = absoluteOffset.x >= absoluteOffset.y;
    const uint axis = dominantX ? (offset.x >= 0 ? 0u : 1u) : (offset.y >= 0 ? 2u : 3u);
    const uint dominant = uint(dominantX ? absoluteOffset.x : absoluteOffset.y);
    const uint minor = uint((dominantX ? offset.y : offset.x) + 16384);
    return (dominant << 17u) | (minor << 2u) | axis;
}

int2 PPRDecodeOffset(uint packed)
{
    const int dominant = int(packed >> 17u);
    const int minor = int((packed >> 2u) & 32767u) - 16384;
    const uint axis = packed & 3u;
    if (axis == 0u) return int2(dominant, minor);
    if (axis == 1u) return int2(-dominant, minor);
    if (axis == 2u) return int2(minor, dominant);
    return int2(minor, -dominant);
}

#endif
