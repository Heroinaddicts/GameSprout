/* GameSproutShader {
    "Name": "Woldmere/Valley Clouds",
    "Passes": [{ "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
        "Cull": "Off", "Blend": "Alpha", "DepthTest": true, "DepthWrite": false, "DepthCompare": "LessEqual" }]
} */
/* GameSproutMaterial {
    "Name": "Woldmere/Valley Clouds",
    "Properties": [{ "Name": "CloudTexture", "DisplayName": "云层贴图", "Type": "Texture", "Default": null }]
} */

cbuffer GameSproutDraw : register(b0, space1) {
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
};

struct SkyVertex {
    [[vk::location(0)]] float3 Direction : TEXCOORD0;
    [[vk::location(3)]] float2 UV : TEXCOORD3;
};
struct SkySample {
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

SkySample VSMain(SkyVertex input) {
    SkySample output;
    // A direction (w = 0) ignores model and camera translation. Place cloud
    // fragments at the far plane so terrain and characters occlude them.
    float4 direction = mul(Model, float4(input.Direction, 0));
    output.Position = mul(ViewProjection, direction);
    output.Position.z = output.Position.w;
    output.UV = input.UV;
    return output;
}

Texture2D<float4> CloudTexture : register(t0, space2);
SamplerState CloudSampler : register(s0, space2);

float4 PSMain(SkySample input) : SV_Target0 {
    return CloudTexture.Sample(CloudSampler, input.UV);
}
