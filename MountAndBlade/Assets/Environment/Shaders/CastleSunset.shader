/* GameSproutShader {
    "Name": "MountAndBlade/Castle Sunset",
    "Passes": [{
        "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
        "Cull": "Off", "Blend": "Alpha", "DepthTest": false, "DepthWrite": false
    }]
} */
/* GameSproutMaterial {
    "Name": "MountAndBlade/Castle Sunset",
    "Properties": [
        {"Name": "SunsetWeight", "DisplayName": "落日强度", "Type": "Float", "Default": 0},
        {"Name": "SunsetDirection", "DisplayName": "落日方向", "Type": "Vector", "Default": [-0.930, 0.174, 0.324, 0]}
    ]
} */

cbuffer SunsetMaterial : register(b0, space3) {
    float SunsetWeight;
    float4 SunsetDirection;
};

struct SkySample {
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 WorldRay : TEXCOORD0;
};

SkySample VSMain(uint vertexID : SV_VertexID) {
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    SkySample output;
    output.Position = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    output.WorldRay = float3(output.Position.xy, 1.0);
    return output;
}

float4 PSMain(SkySample input) : SV_Target0 {
    if (SunsetWeight <= 0.0) return 0.0;

    float3 ray = normalize(input.WorldRay);
    float3 sun = normalize(SunsetDirection.xyz);
    float height = max(ray.y, 0.0);
    float facing = saturate(dot(ray, sun));
    float angularDistance = max(0.0, 1.0 - facing);
    float warmHorizon = exp2(-height * 16.0) * (0.24 + 0.76 * facing * facing);
    float3 upperSky = lerp(float3(0.31, 0.28, 0.41), float3(0.077, 0.146, 0.315),
        smoothstep(0.0, 0.48, height));
    float3 color = upperSky + warmHorizon * float3(1.55, 0.39, 0.11);

    float halo = exp2(-angularDistance * 54.0);
    float aureole = exp2(-angularDistance * 480.0);
    color += halo * float3(0.54, 0.17, 0.061);
    color += aureole * float3(1.9, 0.71, 0.24);
    // A soft inner edge is kept narrow enough to read as a distinct disc.
    float edge = max(fwidth(angularDistance) * 0.7, 0.000018);
    float disc = 1.0 - smoothstep(0.00036 - edge, 0.00036 + edge, angularDistance);
    color += disc * float3(12.0, 6.5, 2.6);

    // Let the stock moving cloud layers remain visible across the transition.
    float opacity = lerp(0.52, 0.78, warmHorizon);
    opacity = max(opacity, 0.95 * disc);
    return float4(color, saturate(opacity * SunsetWeight));
}
