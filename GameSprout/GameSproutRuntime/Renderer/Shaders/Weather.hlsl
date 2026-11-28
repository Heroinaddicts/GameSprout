cbuffer WeatherSkyUniform : register(b0, space3)
{
    float4 WeatherZenith;
    float4 WeatherHorizon;
    float4 WeatherCloudTint;
    float4 WeatherSunDirection;
    float4 WeatherSunRadiance;
    float4 WeatherSkyCubeParameters;
    float4 WeatherStars; // Inverse yaw XY, intensity, density.
    float4 WeatherTime;  // Daylight, direct-sun multiplier, flash intensity.
    float4 WeatherSkyFlashTint;
    float4 WeatherClouds[2];
    float4 WeatherCloudOffsets;
};
Texture2D<float4> WeatherCloudMap : register(t0, space2);
SamplerState WeatherCloudSampler : register(s0, space2);
TextureCube<float4> WeatherSkyCube : register(t1, space2);
SamplerState WeatherSkyCubeSampler : register(s1, space2);

float WeatherHash(float2 cell)
{
    float3 bits = frac(float3(cell.xyx) * 0.1031);
    bits += dot(bits, bits.yzx + 33.33);
    return frac((bits.x + bits.y) * bits.z);
}

float3 WeatherStarRadiance(float3 ray)
{
    float darkness = 1.0 - smoothstep(0.0, 0.40, WeatherTime.x);
    if (darkness <= 0.0 || WeatherStars.z <= 0.0 || WeatherStars.w <= 0.0) return 0.0;
    float3 sky = InverseSkyYaw(ray, WeatherStars.xy);
    // Upper hemisphere of an octahedral map. Each cell carries one potential
    // star, so the pattern stays fixed in direction regardless of camera position.
    float2 xy = sky.xz * (144.0 / (abs(sky.x) + abs(sky.y) + abs(sky.z)));
    float2 cell = floor(xy);
    float seed = WeatherHash(cell);
    float2 centre = 0.27 + 0.46 * float2(WeatherHash(cell + float2(41.0, 17.0)),
        WeatherHash(cell + float2(69.0, 3.0)));
    float distanceToStar = length(frac(xy) - centre);
    float starWidth = lerp(0.028, 0.082, seed * seed);
    float pixel = max(length(fwidth(xy)) * 0.5, 0.012);
    float star = (1.0 - smoothstep(starWidth, starWidth + min(pixel, 0.12), distanceToStar)) *
        min(starWidth / pixel, 1.0) * step(1.0 - WeatherStars.w * 0.28, seed);
    const float3 tint = lerp(float3(0.65, 0.80, 1.0), float3(1.0, 0.87, 0.70), WeatherHash(cell + 121.0));
    return tint * (star * WeatherStars.z * darkness * smoothstep(0.02, 0.18, ray.y)) * (0.50 + seed * 0.65);
}

float3 EvaluateWeatherSky(float3 ray, float3 daySky)
{
    float height = pow(saturate(ray.y), 0.65);
    float daylight = WeatherTime.x;
    float3 nightSky = lerp(float3(0.027, 0.042, 0.083), float3(0.005, 0.010, 0.030), height);
    float3 flash = WeatherSkyFlashTint.rgb * WeatherTime.z;
    float3 color = lerp(nightSky, daySky, daylight) + WeatherStarRadiance(ray);
    float lightFacing = dot(ray, WeatherSunDirection.xyz);
    if (WeatherSunDirection.w > 0.5 && WeatherSunDirection.y > -0.05 && daylight > 0.0)
    {
        float edge = max(fwidth(lightFacing), 0.00002);
        float disc = smoothstep(0.9998 - edge, 0.9998 + edge, lightFacing);
        float glow = pow(saturate(lightFacing), 96.0) * 0.05;
        color += min(WeatherSunRadiance.rgb, 4.0) * ((disc * 0.60 + glow) * daylight * WeatherTime.y);
    }
    // Cloud projections approach the horizon gradually; a grazing ray cannot
    // amplify texture-coordinate derivatives to infinity.
    float2 plane = ray.xz / max(0.25, ray.y + 0.42);
    float horizon = smoothstep(-0.026, 0.17, ray.y);
    [unroll] for (int layer = 1; layer >= 0; --layer)
    {
        float3 settings = WeatherClouds[layer].xyz;
        if (settings.x <= 0.0 || settings.y <= 0.0) continue;
        float2 offset = layer == 0 ? WeatherCloudOffsets.xy : WeatherCloudOffsets.zw;
        float4 noise = WeatherCloudMap.Sample(WeatherCloudSampler, plane * settings.z * 0.5 + offset);
        float sampleValue = layer == 0 ? noise.r : noise.g;
        float threshold = 1.0 - settings.x;
        float mask = smoothstep(threshold - 0.085, threshold + 0.07, sampleValue) *
            smoothstep(0.0, 0.10, settings.x) * settings.y * horizon * WeatherCloudTint.a;
        float body = saturate((sampleValue - threshold) * 3.2 + 0.45);
        float3 nightCloud = lerp(float3(0.017, 0.024, 0.045), float3(0.039, 0.049, 0.078), body);
        float3 dayCloud = lerp(float3(0.24, 0.30, 0.38), float3(0.96, 0.97, 0.98), body);
        dayCloud *= lerp(0.52, 1.0, saturate(WeatherTime.y));
        dayCloud += 0.07 * saturate(lightFacing) * body;
        color = lerp(color, lerp(nightCloud, dayCloud, daylight) * WeatherCloudTint.rgb + flash * 0.060, mask);
    }
    return color + flash * lerp(0.022, 0.040, height);
}

float4 WeatherSkyFragment(SkyOutput input) : SV_Target0
{
    float3 ray = normalize(input.RayDirection);
    float3 base = lerp(WeatherHorizon.rgb, WeatherZenith.rgb, pow(saturate(ray.y), 0.65));
    return float4(EvaluateWeatherSky(ray, base), 1.0);
}

float4 WeatherCubemapSkyFragment(SkyOutput input) : SV_Target0
{
    float3 ray = normalize(input.RayDirection);
    float3 cubeRay = InverseSkyYaw(ray, WeatherSkyCubeParameters.xy);
    float3 base = WeatherSkyCube.Sample(WeatherSkyCubeSampler, cubeRay).rgb * WeatherSkyCubeParameters.z;
    return float4(EvaluateWeatherSky(ray, base), 1.0);
}

cbuffer WeatherLightningVertices : register(b0, space1)
{
    float4x4 WeatherLightningViewProjection;
    float4 WeatherLightningCameraAndWidth;
    float4 WeatherLightningCameraRight;
    float4 WeatherLightningPoints[13];
};
cbuffer WeatherLightningRadiance : register(b0, space3)
{
    float4 WeatherBoltColor;
};
struct WeatherLightningOutput
{
    float4 Position : SV_Position;
    float Across : TEXCOORD0;
};

WeatherLightningOutput WeatherLightningVertex(uint vertexID : SV_VertexID)
{
    uint segment = vertexID / 6;
    uint corner = vertexID % 6;
    uint endpoint = segment + (corner == 1 || corner == 2 || corner == 4 ? 1 : 0);
    float across = (corner == 0 || corner == 1 || corner == 3) ? -1.0 : 1.0;
    float3 center = WeatherLightningPoints[endpoint].xyz;
    float3 tangent = WeatherLightningPoints[min(endpoint + 1, 12)].xyz -
        WeatherLightningPoints[max(int(endpoint) - 1, 0)].xyz;
    float3 side = cross(tangent, WeatherLightningCameraAndWidth.xyz - center);
    float sideLength = dot(side, side);
    side = sideLength > 1e-8 ? side * rsqrt(sideLength) : WeatherLightningCameraRight.xyz;
    float width = WeatherLightningCameraAndWidth.w * lerp(0.50, 1.0, endpoint / 12.0);
    WeatherLightningOutput output;
    output.Position = mul(WeatherLightningViewProjection, float4(center + across * width * side, 1.0));
    output.Across = across;
    return output;
}

float4 WeatherLightningFragment(WeatherLightningOutput input) : SV_Target0
{
    float across = abs(input.Across);
    float glow = (1.0 - across) * (1.0 - across);
    float core = 1.0 - smoothstep(0.12, 0.31, across);
    float alpha = saturate((0.14 * glow + 0.72 * core) * max(WeatherBoltColor.rgb.r, WeatherBoltColor.rgb.b));
    return float4(WeatherBoltColor.rgb * (0.13 * glow + core), alpha);
}
