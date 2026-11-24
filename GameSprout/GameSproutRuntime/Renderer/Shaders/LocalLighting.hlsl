cbuffer SpotShadowUniform : register(b3, space3)
{
    float4x4 SpotWorldToShadow[4];
    float4 SpotShadowAtlas[4];
    float4 SpotShadowParameters[4];
    float4 SpotShadowDistance[4];
};
Texture2D<float> SpotShadowTexture : register(t4, space2);
SamplerState SpotShadowSampler : register(s4, space2);

float LocalLightAttenuation(float3 delta, float distanceSquared, float range)
{
    float3 normalizedDelta = delta / range;
    float rangeFractionSquared = dot(normalizedDelta, normalizedDelta);
    float smoothRange = saturate(1.0 - rangeFractionSquared * rangeFractionSquared);
    return smoothRange * smoothRange / max(distanceSquared, 0.01);
}

float SampleSpotShadow(float2 uv, float reference, float2 lower, float2 upper)
{
    float depth = SpotShadowTexture.SampleLevel(SpotShadowSampler, clamp(uv, lower, upper), 0).r;
    return reference <= depth ? 1.0 : 0.0;
}

float SpotShadow(int slot, float3 position, float3 geometricNormal)
{
    float4 parameters = SpotShadowParameters[slot];
    float4 distance = SpotShadowDistance[slot];
    float cameraDistance = length(CameraPosition.xyz - position);
    if (cameraDistance >= distance.x) return 1.0;
    float4 clip = mul(SpotWorldToShadow[slot], float4(position, 1.0));
    if (clip.w <= 0.0) return 1.0;
    float worldBias = parameters.z * clip.w;
    clip += mul(SpotWorldToShadow[slot], float4(geometricNormal * worldBias, 0.0));
    if (clip.w <= 0.0) return 1.0;
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    float4 tile = SpotShadowAtlas[slot];
    float2 texel = distance.zw;
    float2 uv = (ndc.xy * float2(0.5, -0.5) + 0.5) * tile.xy + tile.zw;
    float2 lower = tile.zw + 0.5 * texel;
    float2 upper = tile.zw + tile.xy - 0.5 * texel;
    float reference = ndc.z - parameters.y / clip.w;
    int filter = int(parameters.w);
    float visibility = 0.0;
    if (filter == 0)
        visibility = SampleSpotShadow(uv, reference, lower, upper);
    else
    {
        int side = filter == 3 ? 5 : filter + 1;
        [loop] for (int y = 0; y < side; ++y)
            [loop] for (int x = 0; x < side; ++x)
                visibility += SampleSpotShadow(uv + (float2(x, y) - (side - 1) * 0.5) * texel, reference, lower, upper);
        visibility /= side * side;
    }
    float fade = saturate((distance.x - cameraDistance) / max(distance.x - distance.y, 0.0001));
    return lerp(1.0, visibility, parameters.x * fade);
}
