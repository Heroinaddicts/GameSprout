Texture2D<float4> HDRSceneColor : register(t0, space2);
SamplerState HDRSceneSampler : register(s0, space2);
cbuffer HDRResolveUniform : register(b0, space3)
{
    float4 ResolveParameters; // Exposure, operator, inverse framebuffer size XY.
};

float4 HDRResolveVertex(uint vertexID : SV_VertexID) : SV_Position
{
    float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(corner * 2.0 - 1.0, 1.0, 1.0);
}

float3 NeutralToneMap(float3 color)
{
    // Khronos PBR Neutral curve. Apply a shared highlight scale so RGB
    // ratios stay stable until bright colors roll toward white.
    float darkest = min(color.r, min(color.g, color.b));
    float offset = darkest < 0.08 ? darkest - 6.25 * darkest * darkest : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    const float compressionStart = 0.76;
    if (peak < compressionStart) return color;
    const float remaining = 1.0 - compressionStart;
    float compressed = 1.0 - remaining * remaining / (peak + remaining - compressionStart);
    color *= compressed / peak;
    float desaturation = 1.0 - rcp(1.0 + 0.15 * (peak - compressed));
    return lerp(color, compressed.xxx, desaturation);
}

float3 ACESToneMap(float3 color)
{
    // Narkowicz's analytic fit of the ACES response. Limit the input so the fit
    // remains finite for a very bright source or a high exposure EV.
    color = min(color, 4096.0);
    return saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
}

float4 HDRResolveFragment(float4 position : SV_Position) : SV_Target0
{
    // Fragment position and sampled color both use framebuffer pixel centers.
    float4 source = HDRSceneColor.SampleLevel(HDRSceneSampler, position.xy * ResolveParameters.zw, 0.0);
    float3 color = min(max(source.rgb, 0.0), 65504.0) * ResolveParameters.x;
    int toneMapping = (int)ResolveParameters.y;
    if (toneMapping == 1) color = NeutralToneMap(color);
    else if (toneMapping == 2) color = ACESToneMap(color);
    return float4(color, source.a);
}
