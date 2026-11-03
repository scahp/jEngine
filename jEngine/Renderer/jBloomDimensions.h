#pragma once

namespace jBloomDimensions
{
    // Bloom uses positive downsample factors and keeps a one-texel minimum on
    // each axis, including every subsequent level of a narrow render target.
    constexpr int Downsample(int Dimension, int Factor)
    {
        const int Result = Dimension / Factor;
        return Result > 0 ? Result : 1;
    }
}
