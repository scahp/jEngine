#include "common.hlsl"
// Hi-Z compute shader injection point; declarations precede shader bodies.
#ifndef JE_HIZ_PASS
#define JE_HIZ_PASS 0
#endif

#if JE_HIZ_PASS == 0 || JE_HIZ_PASS == 1
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    const uint2 destinationSize = uint2(HiZDispatch.Destination.xy);
    if (any(thread.xy >= destinationSize)) return;
    const uint destination = uint(HiZDispatch.Destination.z) + thread.y * destinationSize.x + thread.x;
#if JE_HIZ_PASS == 0
    const uint2 viewport = uint2(HiZDispatch.Viewport.xy);
    float depth = 1.0;
    if (all(thread.xy < viewport))
    {
        depth = HiZDepthInput.Load(int3(thread.xy, 0)).r;
        if (!isfinite(depth) || depth < 0.0 || depth > 1.0) depth = 1.0;
    }
    HiZPyramidOutput[destination] = depth;
#else
    const uint2 sourceSize = uint2(HiZDispatch.Source.xy);
    const uint sourceOffset = uint(HiZDispatch.Source.z);
    float maximum = 0.0;
    [unroll] for (uint y = 0; y < 2; ++y)
    [unroll] for (uint x = 0; x < 2; ++x)
    {
        const uint2 pixel = min(thread.xy * 2 + uint2(x, y), sourceSize - 1);
        const float depth = HiZPyramidOutput[sourceOffset + pixel.y * sourceSize.x + pixel.x];
        maximum = max(maximum, isfinite(depth) && depth >= 0.0 && depth <= 1.0 ? depth : 1.0);
    }
    HiZPyramidOutput[destination] = maximum;
#endif
}
#elif JE_HIZ_PASS == 2
[numthreads(64, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    const uint candidateCount = uint(HiZDispatch.Control.x);
    if (thread.x >= candidateCount) return;
    const jHiZCandidate candidate = HiZCandidates[thread.x];
    jHiZIndexedArguments arguments;
    arguments.IndexCount = candidate.IndexCount;
    arguments.InstanceCount = candidate.InstanceCount;
    arguments.FirstIndex = candidate.FirstIndex;
    arguments.BaseVertex = candidate.BaseVertex;
    arguments.FirstInstance = candidate.FirstInstance;
    bool occluded = false;
    const uint2 viewport = uint2(HiZDispatch.Viewport.xy);
    const uint levelCount = min(uint(HiZDispatch.Control.z), 14u);
    if (HiZDispatch.Control.w > 0.5 && candidate.Testable != 0 && levelCount > 0 &&
        candidate.MinimumX <= candidate.MaximumX && candidate.MinimumY <= candidate.MaximumY &&
        candidate.MaximumX < viewport.x && candidate.MaximumY < viewport.y &&
        isfinite(candidate.NearestDepth) && candidate.NearestDepth > 0.0 && candidate.NearestDepth < 1.0)
    {
        uint level = 0;
        [loop] while (level + 1 < levelCount &&
            ((candidate.MaximumX >> level) - (candidate.MinimumX >> level) > 1 ||
             (candidate.MaximumY >> level) - (candidate.MinimumY >> level) > 1)) ++level;
        const uint3 dimensions = uint3(HiZDispatch.Levels[level].xyz);
        const uint2 first = uint2(candidate.MinimumX, candidate.MinimumY) >> level;
        const uint2 last = uint2(candidate.MaximumX, candidate.MaximumY) >> level;
        float maximum = 0.0;
        [unroll] for (uint y = 0; y < 2; ++y)
        [unroll] for (uint x = 0; x < 2; ++x)
        {
            const uint2 pixel = uint2(x == 0 ? first.x : last.x, y == 0 ? first.y : last.y);
            float depth = 1.0;
            if (all(pixel < dimensions.xy)) depth = HiZPyramidInput[dimensions.z + pixel.y * dimensions.x + pixel.x];
            maximum = max(maximum, isfinite(depth) && depth >= 0.0 && depth <= 1.0 ? depth : 1.0);
        }
        occluded = candidate.NearestDepth > maximum + HiZDispatch.Control.y;
    }
    // Each record belongs to one draw of this view; no shared mesh state is
    // mutated and no CPU waits for visibility or dispatch-count readback.
    arguments.InstanceCount = occluded ? 0u : arguments.InstanceCount;
    HiZArguments[thread.x] = arguments;
}
#else
#error JE_HIZ_PASS must be 0 (seed), 1 (max reduce), or 2 (cull indexed arguments).
#endif
