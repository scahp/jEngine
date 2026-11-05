#ifndef JE_MATERIAL_MAPPING_FUNCTIONS
#define JE_MATERIAL_MAPPING_FUNCTIONS

// Bounded material-space algorithms adapted from scahp/Shadows (MIT):
// ParallaxMapping e884163, HorizonMapping cfac2ac, ReliefMapping 9acb775.
// No resource ownership, render scheduling or scene/global light state lives here.
float3 JE_SafeNormalTS(float3 value)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1e-10 ? value * rsqrt(lengthSquared) : float3(0, 0, 1);
}
float3 JE_DecodeNormalTS(float3 sampleValue, float flipY)
{
    if (flipY > 0.5) sampleValue.y = 1.0 - sampleValue.y;
    return JE_SafeNormalTS(sampleValue * 2.0 - 1.0);
}
// TBN maps tangent to world. Its rows need not form an orthonormal basis after
// authored nonuniform scale/shear, so transpose(TBN) is not its inverse.
float3 JE_WorldNormalToTangent(float3 normalWS, float3x3 basis)
{
    const float3 c0 = cross(basis[1], basis[2]);
    const float3 c1 = cross(basis[2], basis[0]);
    const float3 c2 = cross(basis[0], basis[1]);
    const float determinant = dot(basis[0], c0);
    if (abs(determinant) < 1e-8) return float3(0, 0, 1);
    return JE_SafeNormalTS(mul(transpose(float3x3(c0, c1, c2)), normalWS) / determinant);
}
// basisSpace.xyz is an explicitly authored source-axis sign correction; w is
// Tangent=0/Object=1/World=2. Samples always come from the linear Normal binding.
// Object normals use the same inverse-transpose convention as the mesh normal.
float3 JE_DecodeMaterialNormal(float3 sampleValue, JEMaterialContext ctx, float4 basisSpace, float flipY)
{
    const float3 decoded = JE_SafeNormalTS(JE_DecodeNormalTS(sampleValue, flipY) * basisSpace.xyz);
    const uint space = (uint)basisSpace.w;
    if (space == 0u) return decoded;
    const float3 normalWS = space == 1u
        ? JE_SafeNormalTS(mul(decoded, (float3x3)RenderObjectParam.InvM)) : decoded;
    return JE_WorldNormalToTangent(normalWS, ctx.TBN);
}
float4 JE_LinearColor(float4 value)
{
    return float4(pow(max(value.rgb, 0.0), 2.2), value.a);
}
float2 JE_ViewSlope(float3 viewTS, float flipV)
{
    float2 slope = viewTS.xy / max(abs(viewTS.z), 0.05);
    if (flipV > 0.5) slope.y = -slope.y;
    return clamp(slope, -20.0, 20.0);
}
// params = height scale in UV units, iterations [1,128], reserved, flip V.
float2 JE_Parallax(Texture2D heightMap, SamplerState heightSampler, float2 uv, float3 viewTS, float4 parameters)
{
    const int steps = clamp((int)parameters.y, 1, 128);
    const float2 offset = JE_ViewSlope(viewTS, parameters.w) * parameters.x / steps;
    [loop] for (int i = 0; i < 128; ++i)
    {
        if (i >= steps) break;
        const float height = saturate(heightMap.SampleLevel(heightSampler, uv, 0).r) - 0.5;
        uv += offset * height;
    }
    return uv;
}
float2 JE_TraceHeight(Texture2D heightMap, SamplerState heightSampler, float2 uv, float3 viewTS,
    float scale, int steps, int refinements, float flipV, bool dualDepth)
{
    steps = clamp(steps, 1, 128);
    refinements = clamp(refinements, 0, 8);
    const float2 direction = JE_ViewSlope(viewTS, flipV) * scale;
    float low = 0.0, high = 1.0;
    bool hit = false;
    [loop] for (int i = 1; i <= 128; ++i)
    {
        if (i > steps) break;
        const float depth = (float)i / steps;
        const float2 rayUV = uv - direction * depth;
        const float2 sampleValue = saturate(heightMap.SampleLevel(heightSampler, rayUV, 0).rg);
        const float front = 1.0 - sampleValue.r;
        const float back = dualDepth ? max(front, sampleValue.g) : 1.0;
        if (depth >= front && depth <= back)
        {
            high = depth; low = (float)(i - 1) / steps; hit = true; break;
        }
    }
    if (!hit) return uv - direction;
    [loop] for (int refine = 0; refine < 8; ++refine)
    {
        if (refine >= refinements) break;
        const float depth = (low + high) * 0.5;
        const float front = 1.0 - saturate(heightMap.SampleLevel(heightSampler, uv - direction * depth, 0).r);
        if (depth >= front) high = depth;
        else low = depth;
    }
    return uv - direction * ((low + high) * 0.5);
}
// params = height scale, linear steps [1,128], binary refinements [0,8], flip V.
float2 JE_POM(Texture2D heightMap, SamplerState heightSampler, float2 uv, float3 viewTS, float4 parameters)
{
    return JE_TraceHeight(heightMap, heightSampler, uv, viewTS, parameters.x,
        (int)parameters.y, (int)parameters.z, parameters.w, false);
}
// params.w selects single-depth R (0) or front/back RG (1). The front channel
// stores 1-depth and the back channel stores depth, matching the source recipe.
float2 JE_Relief(Texture2D depthMap, SamplerState depthSampler, float2 uv, float3 viewTS, float4 parameters)
{
    return JE_TraceHeight(depthMap, depthSampler, uv, viewTS, parameters.x,
        (int)parameters.y, (int)parameters.z, 0.0, parameters.w > 0.5);
}
float2 JE_RoomBoxUV(float2 uv, float3 viewTS, float depth)
{
    const float3 start = float3(frac(uv) * 2.0 - 1.0, -depth);
    const float3 direction = JE_SafeNormalTS(float3(-viewTS.xy, max(abs(viewTS.z), 0.05)));
    const float3 extent = float3(1.0, 1.0, depth);
    const float3 safeDirection = float3(abs(direction.x) > 1e-5 ? direction.x : 1e-5,
        abs(direction.y) > 1e-5 ? direction.y : 1e-5, max(direction.z, 1e-5));
    const float3 distanceToWall = (sign(safeDirection) * extent - start) / safeDirection;
    const float distanceToHit = min(distanceToWall.x, min(distanceToWall.y, distanceToWall.z));
    const float3 hit = start + direction * max(distanceToHit, 0.0);
    if (distanceToWall.x < distanceToWall.y && distanceToWall.x < distanceToWall.z)
        return float2(hit.z / depth, hit.y) * 0.5 + 0.5;
    if (distanceToWall.y < distanceToWall.z)
        return float2(hit.x, hit.z / depth) * 0.5 + 0.5;
    return hit.xy * 0.5 + 0.5;
}
// params = room count U/V (0,256], room depth [.001,64], ambient multiplier [0,4].
float4 JE_InteriorImpl(Texture2D roomMap, SamplerState roomSampler, float2 uv, float3 viewTS, float4 parameters, bool srgb)
{
    const float2 roomUV = JE_RoomBoxUV(uv * parameters.xy, viewTS, max(parameters.z, 0.001));
    const float4 sampleValue = roomMap.SampleLevel(roomSampler, roomUV, 0);
    return float4((srgb ? JE_LinearColor(sampleValue).rgb : sampleValue.rgb) * parameters.w, sampleValue.a);
}
float4 JE_Interior(Texture2D roomMap, SamplerState roomSampler, float2 uv, float3 viewTS, float4 parameters)
{ return JE_InteriorImpl(roomMap, roomSampler, uv, viewTS, parameters, true); }
float4 JE_InteriorLinear(Texture2D roomMap, SamplerState roomSampler, float2 uv, float3 viewTS, float4 parameters)
{ return JE_InteriorImpl(roomMap, roomSampler, uv, viewTS, parameters, false); }
float4 JE_InteriorDualImpl(Texture2D colorMap, SamplerState colorSampler, Texture2D depthMap,
    SamplerState depthSampler, Texture2D backgroundMap, SamplerState backgroundSampler,
    float2 uv, float3 viewTS, float4 parameters, bool srgb)
{
    const float2 localUV = frac(uv * parameters.xy);
    const float2 mapped = JE_TraceHeight(depthMap, depthSampler, localUV, viewTS,
        parameters.z, 128, 6, 0.0, true);
    const float2 depths = saturate(depthMap.SampleLevel(depthSampler, mapped, 0).rg);
    const bool inRange = all(mapped >= 0.0) && all(mapped <= 1.0);
    const bool hasDepth = depths.r > 0.0 && depths.g > 0.0;
    if (inRange && hasDepth)
    {
        const float4 sampleValue = colorMap.SampleLevel(colorSampler, mapped, 0);
        return float4((srgb ? JE_LinearColor(sampleValue).rgb : sampleValue.rgb) * parameters.w, sampleValue.a);
    }
    return JE_InteriorImpl(backgroundMap, backgroundSampler, uv, viewTS, parameters, srgb);
}
float4 JE_InteriorDual(Texture2D colorMap, SamplerState colorSampler, Texture2D depthMap,
    SamplerState depthSampler, Texture2D backgroundMap, SamplerState backgroundSampler,
    float2 uv, float3 viewTS, float4 parameters)
{ return JE_InteriorDualImpl(colorMap, colorSampler, depthMap, depthSampler, backgroundMap, backgroundSampler, uv, viewTS, parameters, true); }
float4 JE_InteriorDualLinear(Texture2D colorMap, SamplerState colorSampler, Texture2D depthMap,
    SamplerState depthSampler, Texture2D backgroundMap, SamplerState backgroundSampler,
    float2 uv, float3 viewTS, float4 parameters)
{ return JE_InteriorDualImpl(colorMap, colorSampler, depthMap, depthSampler, backgroundMap, backgroundSampler, uv, viewTS, parameters, false); }
// Baker k=0..7 follows +U,+V,-U,-V, at 2*PI*k/8. Layer channels store
// horizon angle/(PI/2); AO is mean(cos(horizon)^2), never baked into albedo.
// params = softness radians [.001,PI/2], AO strength [0,1], angle scale [0,4], 0.
float2 JE_Horizon(Texture2D layer0, SamplerState sampler0, Texture2D layer1, SamplerState sampler1,
    Texture2D ambientMap, SamplerState ambientSampler, float2 uv, float3 lightTS, float4 parameters)
{
    const float4 angles0 = layer0.SampleLevel(sampler0, uv, 0);
    const float4 angles1 = layer1.SampleLevel(sampler1, uv, 0);
    const float angleIndex = frac(atan2(lightTS.y, lightTS.x) * 0.159154943091895) * 8.0;
    const uint a = (uint)floor(angleIndex) & 7u;
    const uint b = (a + 1u) & 7u;
    const float angleA = a < 4u ? angles0[a & 3u] : angles1[a & 3u];
    const float angleB = b < 4u ? angles0[b & 3u] : angles1[b & 3u];
    const float horizon = lerp(angleA, angleB, frac(angleIndex)) * 1.570796326794897 * parameters.z;
    const float xyLength = length(lightTS.xy);
    const float elevation = atan2(max(lightTS.z, 0.0), xyLength);
    const float softness = max(parameters.x, 0.001);
    float visibility = smoothstep(horizon - softness, horizon + softness, elevation);
    if (lightTS.z <= 0.0) visibility = 0.0;
    else if (xyLength < 1e-6) visibility = 1.0;
    const float ambient = lerp(1.0, saturate(ambientMap.SampleLevel(ambientSampler, uv, 0).r), saturate(parameters.y));
    return float2(saturate(visibility), ambient);
}
#endif
