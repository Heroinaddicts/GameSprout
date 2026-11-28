/* GameSproutShader {
    "Name": "MountAndBlade/Castle Rain Impact",
    "Passes": [{
        "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
        "Cull": "Off", "Blend": "Alpha", "DepthTest": true, "DepthWrite": false
    }]
} */
/* GameSproutMaterial {
    "Name": "MountAndBlade/Castle Rain Impact",
    "Properties": [
        {"Name": "WaterMode", "DisplayName": "水面涟漪模式", "Type": "Float", "Default": 0},
        {"Name": "ImpactTint", "DisplayName": "雨滴色调", "Type": "Color", "Default": [0.60, 0.77, 0.96, 1]}
    ]
} */

cbuffer GameSproutDraw : register(b0, space1) {
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
};

cbuffer RainImpactMaterial : register(b0, space3) {
    float WaterMode;
    float4 ImpactTint;
};

struct MeshVertex {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
};

struct ImpactSample {
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 Local : TEXCOORD0;
    [[vk::location(1)]] float Age : TEXCOORD1;
    [[vk::location(2)]] float Seed : TEXCOORD2;
};

ImpactSample VSMain(MeshVertex input) {
    ImpactSample output;
    float4 center = mul(Model, float4(0, 0, 0, 1));
    output.Position = mul(ViewProjection, mul(Model, float4(input.Position, 1)));
    output.Local = input.Position.xy * 2;
    // The quad stays in its model XY plane. The unused perpendicular scale carries its normalized age.
    output.Age = saturate(length(mul(Model, float4(0, 0, 1, 0)).xyz) - 0.05);
    output.Seed = frac(sin(dot(center.xz, float2(21.153, 61.9))) * 1571.0);
    return output;
}

float Ring(float radius, float center, float width, float antialias) {
    return 1 - smoothstep(width, width + antialias, abs(radius - center));
}

float4 WaterRipples(float2 position, float age) {
    float r = length(position);
    float limit = 0.105 + age * 0.80;
    float antialias = clamp(fwidth(r), 0.01, 0.17);
    float spread = 0.036 + (1 - age) * 0.035;
    float crest = Ring(r, limit, spread, antialias);
    float childRing = Ring(r, limit - 0.125, 0.029, antialias) * smoothstep(0.12, 0.36, age);
    float glint = lerp(0.43, 1, saturate(0.5 + position.y * 0.86 + position.x * 0.31));
    float fade = (1 - smoothstep(0.44, 1, age)) * smoothstep(0, 0.08, age);
    float intensity = (crest + childRing * 0.5) * fade * glint;
    return float4(ImpactTint.rgb, saturate(intensity * ImpactTint.a));
}

float DropLine(float2 position, float2 beginning, float2 ending, float width, float aa) {
    float2 segment = ending - beginning;
    float t = saturate(dot(position - beginning, segment) / max(0.0001, dot(segment, segment)));
    return 1 - smoothstep(width, width + aa, length(position - (beginning + segment * t)));
}

float4 GroundSplash(float2 local, float age, float seed) {
    float2 position = float2(local.x, local.y * 0.5 + 0.5);
    float width = 0.016 + 0.021 * (1 - age);
    float aa = clamp(max(fwidth(position.x), fwidth(position.y)) * 0.62, 0.013, 0.13);
    float spread = age * 0.78;
    float splash = 0;
    [unroll] for (int drop = 0; drop < 6; ++drop) {
        float slope = (drop - 2.5) * 0.39 + (seed - 0.5) * 0.20;
        float speed = 0.65 + 0.29 * frac(seed + drop * 0.63);
        float lift = max(0, age * (1.8 + 1.3 * speed) - age * age * 3.0);
        float2 tip = float2(slope * (spread + 0.11), 0.07 + lift * speed);
        float2 tail = tip - float2(slope * 0.089, 0.085 + speed * 0.067) * (1 - age * 0.60);
        splash = max(splash, DropLine(position, max(tail, float2(-1, 0.04)), tip, width * speed, aa));
    }
    float crown = 1 - smoothstep(0.11 + spread * 0.64, 0.18 + spread * 0.68, abs(position.x));
    crown *= (1 - smoothstep(0.075, 0.132, abs(position.y - 0.075))) * (1 - smoothstep(0.12, 0.34, age));
    float fade = smoothstep(0, 0.07, age) * (1 - smoothstep(0.43, 1, age));
    return float4(ImpactTint.rgb, saturate((splash * 0.9 + crown * 0.73) * fade * ImpactTint.a));
}

float4 PSMain(ImpactSample input) : SV_Target0 {
    return WaterMode > 0.5 ? WaterRipples(input.Local, input.Age) : GroundSplash(input.Local, input.Age, input.Seed);
}
