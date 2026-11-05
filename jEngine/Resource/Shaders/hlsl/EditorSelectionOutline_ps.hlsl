float4 main(float4 position : SV_POSITION) : SV_Target0
{
    const int2 pixel = int2(position.xy);
    if (any(SelectionMask.Load(int3(pixel, 0))))
        discard;

    uint width, height;
    SelectionMask.GetDimensions(width, height);
    const int2 lastPixel = int2(width, height) - 1;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 neighbor = clamp(pixel + 2 * int2(x, y), int2(0, 0), lastPixel);
            if (any(SelectionMask.Load(int3(neighbor, 0))))
                return float4(1.0, 0.64, 0.16, 1.0);
        }
    }
    discard;
    return 0.0;
}
