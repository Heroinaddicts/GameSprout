Texture2D<float4> PlanarTexture : register(t7, space2);
SamplerState PlanarSampler : register(s7, space2);

float3 ApplyPlanarReflection(float3 lit, float3 environmentSpecular, float2 pixelPosition, float worldHeight,
                            float3 normal, float3 view, float roughness, float3 f0)
{
    const float strength = PlanarReflection.x * (1.0 - smoothstep(0.04, 0.12, abs(worldHeight - PlanarReflection.w)));
    if (strength <= 0.0) return lit + environmentSpecular;
    const float2 screenTexel = PlanarReflection.yz;
    float2 uv = pixelPosition * screenTexel;
    uv.x = 1.0 - uv.x; // Undo the handedness-preserving flip in the capture view.
    const float2 offset = screenTexel * (4.0 * roughness * roughness);
    float3 reflected = PlanarTexture.Sample(PlanarSampler, uv).rgb * 0.4;
    reflected += PlanarTexture.Sample(PlanarSampler, uv + float2(offset.x, 0)).rgb * 0.15;
    reflected += PlanarTexture.Sample(PlanarSampler, uv - float2(offset.x, 0)).rgb * 0.15;
    reflected += PlanarTexture.Sample(PlanarSampler, uv + float2(0, offset.y)).rgb * 0.15;
    reflected += PlanarTexture.Sample(PlanarSampler, uv - float2(0, offset.y)).rgb * 0.15;
    const float3 fresnel = saturate(f0 + (1.0 - f0) * pow(1.0 - saturate(dot(normal, view)), 5.0));
    return lerp(lit + environmentSpecular, lerp(lit, reflected, fresnel), strength);
}
