/* GameSproutShader {
    "Name": "MountAndBlade/Castle Lightning Detail",
    "Passes": [{
        "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
        "Cull": "Off", "Blend": "PremultipliedAlpha", "DepthTest": true, "DepthWrite": false
    }]
} */
/* GameSproutMaterial {
    "Name": "MountAndBlade/Castle Lightning Detail",
    "Properties": [
        {"Name": "GroundContact", "DisplayName": "落点电弧", "Type": "Float", "Default": 0},
        {"Name": "ArcTint", "DisplayName": "电弧色调", "Type": "Color", "Default": [0.48, 0.70, 1.0, 1.0]}
    ]
} */

cbuffer GameSproutDraw : register(b0, space1) {
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
};

cbuffer LightningSurface : register(b0, space3) {
    float GroundContact;
    float4 ArcTint;
};

struct MeshVertex {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
};

struct LightningSample {
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 Local : TEXCOORD0;
};

LightningSample VSMain(MeshVertex input) {
    LightningSample output;
    output.Position = mul(ViewProjection, mul(Model, float4(input.Position, 1.0)));
    // Local Z scale carries life or brightness. The source quad lies in XY.
    output.Local = float3(input.Position.xy * 2.0, length(mul(Model, float4(0, 0, 1, 0)).xyz));
    return output;
}

float4 PSMain(LightningSample input) : SV_Target0 {
    float2 p = input.Local.xy;
    float state = saturate(input.Local.z);
    if (GroundContact < 0.5) {
        float width = abs(p.x);
        float border = max(fwidth(p.x), 0.013);
        float core = 1.0 - smoothstep(0.18, 0.30 + border, width);
        float halo = exp2(-6.4 * width * width) * (1.0 - smoothstep(0.73, 1.0, width));
        float light = (core * 1.36 + halo * 0.57) * state;
        return float4(lerp(ArcTint.rgb, float3(1.0, 1.0, 1.0), core * 0.76) * light,
            saturate(0.21 * light) * ArcTint.a);
    }

    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float response = exp2(-6.0 * state) * (1.0 - state);
    float hotSpot = exp2(-24.0 * radius) * response * 3.4;
    float pulse = lerp(0.12, 0.85, state);
    float ring = exp2(-61.0 * abs(radius - pulse + sin(angle * 9.0) * 0.025));
    float paths = pow(saturate(0.5 + 0.5 * cos(angle * 13.0 + sin(radius * 19.0) * 0.73)), 15.0);
    float sparks = paths * smoothstep(0.04, 0.15, radius) * (1.0 - smoothstep(0.18, pulse + 0.23, radius));
    float light = hotSpot + (ring * 0.93 + sparks * 1.2) * response;
    return float4(ArcTint.rgb * light, saturate(0.30 * light) * ArcTint.a);
}
