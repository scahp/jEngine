#ifndef JENGINE_GROUND_ATMOSPHERE_HLSL
#define JENGINE_GROUND_ATMOSPHERE_HLSL

// Ground-level single scattering in a finite, Y-up atmospheric slab. Exponential
// Rayleigh/Mie density and the normalized phase functions follow the same model
// family as Shadows/AccurateAtmosphericScattering (MIT) and GPU Gems 2 chapter16.
// This library has no planet mesh, global textures, Earth-scale coordinates or
// mutable state. Distances inside it are meters.
float2 AtmosphereDensity(float height, float2 scaleHeights)
{
    return exp(-max(0.0, height) / max(scaleHeights, 1.0.xx));
}

float AtmosphereRayleighPhase(float cosine)
{
    return 0.0596831036595 * (1.0 + cosine * cosine);
}

float AtmosphereMiePhase(float cosine, float g)
{
    const float g2 = g * g;
    const float denominator = max(1.0e-4, 1.0 + g2 - 2.0 * g * cosine);
    return 0.119366207319 * ((1.0 - g2) / (2.0 + g2)) *
        (1.0 + cosine * cosine) / pow(denominator, 1.5);
}

float2 AtmosphereSunOpticalDepth(float height, float sunY, float atmosphereHeight,
    float maxDistance, float2 scaleHeights, uint lightSamples)
{
    const float distance = min(maxDistance, max(0.0, atmosphereHeight - height) / max(sunY, 0.001));
    const float stepLength = distance / float(lightSamples);
    float2 opticalDepth = 0.0;
    [loop] for (uint i = 0; i < lightSamples; ++i)
    {
        const float sampleHeight = height + sunY * ((float(i) + 0.5) * stepLength);
        opticalDepth += AtmosphereDensity(sampleHeight, scaleHeights) * stepLength;
    }
    return opticalDepth;
}

void IntegrateGroundAtmosphere(float cameraHeight, float3 rayDirection, float distance,
    float atmosphereHeight, float maxLightDistance, float3 betaRayleigh, float3 betaMie,
    float2 scaleHeights, float3 toSun, float mieG, float3 solarIrradiance,
    uint viewSamples, uint lightSamples, out float3 transmittance, out float3 scattering)
{
    transmittance = 1.0;
    scattering = 0.0;
    float enter = 0.0;
    float exit = distance;
    if (abs(rayDirection.y) > 1.0e-6)
    {
        const float t0 = -cameraHeight / rayDirection.y;
        const float t1 = (atmosphereHeight - cameraHeight) / rayDirection.y;
        enter = max(0.0, min(t0, t1));
        exit = min(distance, max(t0, t1));
    }
    else if (cameraHeight < 0.0 || cameraHeight > atmosphereHeight)
        return;
    if (exit <= enter) return;

    const float stepLength = (exit - enter) / float(viewSamples);
    const float cosine = clamp(dot(rayDirection, toSun), -1.0, 1.0);
    const float phaseRayleigh = AtmosphereRayleighPhase(cosine);
    const float phaseMie = AtmosphereMiePhase(cosine, mieG);
    float2 opticalDepth = 0.0;
    [loop] for (uint i = 0; i < viewSamples; ++i)
    {
        const float height = cameraHeight + rayDirection.y * (enter + (float(i) + 0.5) * stepLength);
        const float2 density = AtmosphereDensity(height, scaleHeights);
        const float2 halfViewDepth = opticalDepth + density * (0.5 * stepLength);
        if (toSun.y > 0.0)
        {
            const float2 sunDepth = AtmosphereSunOpticalDepth(height, toSun.y,
                atmosphereHeight, maxLightDistance, scaleHeights, lightSamples);
            const float2 totalDepth = halfViewDepth + sunDepth;
            const float3 attenuation = exp(-min(80.0.xxx,
                betaRayleigh * totalDepth.x + betaMie * totalDepth.y));
            scattering += attenuation * (betaRayleigh * (density.x * phaseRayleigh) +
                betaMie * (density.y * phaseMie)) * (stepLength * solarIrradiance);
        }
        opticalDepth += density * stepLength;
    }
    transmittance = exp(-min(80.0.xxx, betaRayleigh * opticalDepth.x + betaMie * opticalDepth.y));
}

#endif
