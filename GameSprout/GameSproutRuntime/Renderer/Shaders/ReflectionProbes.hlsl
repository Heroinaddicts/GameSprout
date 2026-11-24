TextureCube<float4> ProbeReflection : register(t6, space2);
SamplerState ProbeReflectionSampler : register(s6, space2);

float ReflectionProbeWeight(float3 world)
{
    if (ProbePosition.w == 0.0) return 0.0;
    float3 toEdge = ProbeExtent.xyz - abs(world - ProbeCenter.xyz);
    float edge = min(toEdge.x, min(toEdge.y, toEdge.z));
    if (edge < 0.0) return 0.0;
    return ProbeCenter.w > 0.0 ? saturate(edge / ProbeCenter.w) : 1.0;
}

float3 ProjectReflectionToProbe(float3 world, float3 ray)
{
    float3 offset = world - ProbeCenter.xyz;
    float3 absoluteRay = abs(ray);
    float3 distance = max(ProbeExtent.xyz - sign(ray) * offset, 0.0);
    float3 t = distance / max(absoluteRay, 1e-6);
    // A parallel ray never intersects that slab, even at its edge.
    t.x = absoluteRay.x > 1e-6 ? t.x : 3.402823e38;
    t.y = absoluteRay.y > 1e-6 ? t.y : 3.402823e38;
    t.z = absoluteRay.z > 1e-6 ? t.z : 3.402823e38;
    float3 direction = offset + ray * min(t.x, min(t.y, t.z)) + (ProbeCenter.xyz - ProbePosition.xyz);
    float magnitude = max(abs(direction.x), max(abs(direction.y), abs(direction.z)));
    return magnitude > 1e-6 && isfinite(magnitude) ? direction / magnitude : ray;
}

float3 SampleReflectionProbe(float3 world, float3 ray, float roughness)
{
    if (ProbePosition.w > 0.0) ray = ProjectReflectionToProbe(world, ray);
    float mip = roughness * (abs(ProbePosition.w) - 1.0);
    return ProbeReflection.SampleLevel(ProbeReflectionSampler, ray, mip).rgb * ProbeExtent.w;
}
