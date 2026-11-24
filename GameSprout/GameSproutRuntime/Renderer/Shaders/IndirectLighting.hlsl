Texture2D<float4> IndirectLightingAtlas : register(t8, space2);
SamplerState IndirectLightingSampler : register(s8, space2);

float3 SampleIndirectVolume(IndirectVolume region, float3 uvw, float3 normal)
{
    float3 dimensions = region.GridAndAtlasRow.xyz;
    float3 samplePosition = uvw * (dimensions - 1.0);
    float slice = floor(samplePosition.z);
    float row = region.GridAndAtlasRow.w + samplePosition.y + 0.5;
    float x = samplePosition.x + 0.5;
    float uv0 = (slice * dimensions.x + x) * IndirectInfo.x;
    float uv1 = (min(slice + 1.0, dimensions.z - 1.0) * dimensions.x + x) * IndirectInfo.x;
    float blendZ = samplePosition.z - slice;
    float basis[9] = {1.0, normal.y, normal.z, normal.x, normal.x * normal.y,
        normal.y * normal.z, 3.0 * normal.z * normal.z - 1.0,
        normal.x * normal.z, normal.x * normal.x - normal.y * normal.y};
    float3 result = 0.0;
    [unroll] for (int coefficient = 0; coefficient < 9; ++coefficient)
    {
        float v = (row + coefficient * dimensions.y) * IndirectInfo.y;
        float3 a = IndirectLightingAtlas.SampleLevel(IndirectLightingSampler, float2(uv0, v), 0.0).rgb;
        float3 b = IndirectLightingAtlas.SampleLevel(IndirectLightingSampler, float2(uv1, v), 0.0).rgb;
        result += lerp(a, b, blendZ) * basis[coefficient];
    }
    return max(result, 0.0) * region.InvSizeAndIntensity.w;
}

float3 DiffuseIndirectLighting(float3 worldPosition, float3 normal, float3 globalIrradiance)
{
    float3 result = globalIrradiance;
    // CPU sorts from lower to higher priority. A higher-priority volume
    // composites over the lower ones across its inner border.
    [loop] for (int index = 0; index < (int)IndirectInfo.z; ++index)
    {
        IndirectVolume region = IndirectVolumes[index];
        float3 uvw = (worldPosition - region.MinimumAndInvBlend.xyz) * region.InvSizeAndIntensity.xyz;
        if (any(uvw < 0.0) || any(uvw > 1.0)) continue;
        float weight = 1.0;
        if (region.MinimumAndInvBlend.w > 0.0)
        {
            float3 edge = min(uvw, 1.0 - uvw) / region.InvSizeAndIntensity.xyz;
            weight = saturate(min(min(edge.x, edge.y), edge.z) * region.MinimumAndInvBlend.w);
            if (weight <= 0.0) continue;
            weight = weight * weight * (3.0 - 2.0 * weight);
        }
        result = lerp(result, SampleIndirectVolume(region, uvw, normal), weight);
    }
    return result;
}
