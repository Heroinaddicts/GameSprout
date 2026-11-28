/* GameSproutShader {
    "Name": "MountAndBlade/Castle Aurora",
    "Passes": [{
        "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
        "Cull": "Off", "Blend": "Alpha", "DepthTest": false, "DepthWrite": false
    }]
} */
/* GameSproutMaterial {
    "Name": "MountAndBlade/Castle Aurora",
    "Properties": [
        {"Name": "AuroraIntensity", "DisplayName": "极光强度", "Type": "Float", "Default": 0},
        {"Name": "AuroraTint", "DisplayName": "极光色调", "Type": "Color", "Default": [0.08, 0.78, 0.46, 1]}
    ]
} */

cbuffer AuroraMaterial : register(b0, space3) {
    float AuroraIntensity;
    float4 AuroraTint;
};

cbuffer GameSproutFrame : register(b1, space3) {
    float4 Time;
    float4 CameraPosition;
    float4 MainLightDirection;
    float4 MainLightColor;
};

struct SkySample {
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 WorldRay : TEXCOORD0;
};

SkySample VSMain(uint vertexID : SV_VertexID) {
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    SkySample output;
    output.Position = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    output.WorldRay = float3(output.Position.x, output.Position.y, 1.0);
    return output;
}

float4 PSMain(SkySample input) : SV_Target0 {
    float3 ray = normalize(input.WorldRay);
    float west = smoothstep(0.16, 0.76, -ray.x);
    float azimuth = atan2(ray.z, -ray.x);
    float time = Time.x * 0.33;
    float arc = 0.22 + 0.085 * sin(azimuth * 3.2 + time)
        + 0.033 * sin(azimuth * 9.0 - time * 0.6);
    float height = ray.y - arc;
    float curtain = smoothstep(-0.031, 0.027, height) * (1.0 - smoothstep(0.064, 0.27, height));
    float strands = 0.52 + 0.27 * sin(61.0 * azimuth + 3.6 * sin(azimuth * 19.0 + time))
        + 0.16 * sin(113.0 * azimuth - time * 1.6);
    float secondArc = 0.055 + 0.040 * sin(azimuth * 6.5 - time);
    float ribbon = exp(-abs(height - secondArc) * 24.0) * (0.36 + 0.22 * sin(azimuth * 13.0 + time));
    float alpha = saturate(west * (curtain * strands + ribbon) * max(AuroraIntensity, 0.0)) * AuroraTint.a;
    float palette = smoothstep(0.03, 0.21, height);
    float3 radiance = lerp(AuroraTint.rgb, float3(0.38, 0.15, 0.83), palette);
    return float4(radiance, saturate(alpha));
}
