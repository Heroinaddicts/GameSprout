/* GameSproutShader {
    "Name": "MountAndBlade/Castle Storm",
    "Passes": [{
        "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
        "Cull": "Off", "Blend": "Alpha", "DepthTest": false, "DepthWrite": false
    }]
} */
/* GameSproutMaterial {
    "Name": "MountAndBlade/Castle Storm",
    "Properties": [
        {"Name": "GustMode", "DisplayName": "阵风模式", "Type": "Float", "Default": 0},
        {"Name": "StreakTint", "DisplayName": "雨丝与阵风色调", "Type": "Color", "Default": [0.52, 0.68, 0.84, 0.46]}
    ]
} */

cbuffer GameSproutDraw : register(b0, space1) {
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
};

cbuffer GameSproutFrame : register(b1, space3) {
    float4 Time;
    float4 CameraPosition;
    float4 MainLightDirection;
    float4 MainLightColor;
};

cbuffer StormMaterial : register(b0, space3) {
    float GustMode;
    float4 StreakTint;
};

struct MeshVertex {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
};

struct ScreenSample {
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
    [[vk::location(1)]] float3 OwnerPosition : TEXCOORD1;
    [[vk::location(2)]] float Opacity : TEXCOORD2;
};

ScreenSample VSMain(MeshVertex input) {
    ScreenSample output;
    // The camera child has local Z position equal to its Z scale. The
    // quad remains in front of the camera for CPU visibility tests.
    output.OwnerPosition = mul(Model, float4(0, 0, -1, 1)).xyz;
    output.Opacity = saturate(length(mul(Model, float4(1, 0, 0, 0)).xyz));
    float4 center = mul(ViewProjection, mul(Model, float4(0, 0, 0, 1)));
    bool centered = center.w > 0.0 && all(abs(center.xy) < 0.002 * center.w + 0.0001);
    output.Position = centered ? float4(input.Position.xy * 2.0, 0.0, 1.0)
                               : float4(2.0, 2.0, 0.0, 1.0);
    output.UV = float2(input.Position.x + 0.5, 0.5 - input.Position.y);
    return output;
}

float Hash(float2 cell) {
    float3 value = frac(float3(cell.xyx) * 0.1031);
    value += dot(value, value.yzx + 33.33);
    return frac((value.x + value.y) * value.z);
}

float RainLayer(float2 screen, float time, float density, float speed, float lean, float seed) {
    float2 streakSpace = float2(screen.x - screen.y * lean, screen.y - time * speed) * density;
    float2 cell = floor(streakSpace);
    float random = Hash(cell + seed);
    float2 local = frac(streakSpace);
    float center = lerp(0.18, 0.82, random);
    float width = lerp(0.012, 0.032, random);
    float height = lerp(0.18, 0.58, random);
    float aa = max(fwidth(streakSpace.x), 0.001);
    float across = 1.0 - smoothstep(width, width + aa, abs(local.x - center));
    float end = smoothstep(0.08, 0.16, local.y) * (1.0 - smoothstep(height, height + 0.17, local.y));
    return across * end * lerp(0.29, 1.0, random) * step(0.19, random);
}

float GustLayer(float2 screen, float time, float seed) {
    float2 space = float2((screen.x - time * 1.16) * 2.3,
        (screen.y + screen.x * 0.08 + sin(screen.x * 4.4 + time * 3.0 + seed) * 0.022) * 13.0);
    float2 cell = floor(space);
    float random = Hash(cell + seed);
    float2 local = frac(space);
    float center = lerp(0.16, 0.84, random);
    float across = 1.0 - smoothstep(0.018, 0.052, abs(local.y - center));
    float length = lerp(0.38, 0.79, random);
    float end = smoothstep(0.09, 0.25, local.x) * (1.0 - smoothstep(length - 0.19, length, local.x));
    return across * end * step(0.40, random) * lerp(0.30, 0.93, random);
}

float4 PSMain(ScreenSample input) : SV_Target0 {
    float3 offset = input.OwnerPosition - CameraPosition.xyz;
    if (dot(offset, offset) > 0.0004) discard;

    float aspect = clamp(abs(ddy(input.UV.y)) / max(abs(ddx(input.UV.x)), 0.00001), 0.5, 4.0);
    float2 screen = input.UV * float2(aspect, 1.0);
    float t = Time.x;
    float alpha;
    if (GustMode >= 0.5) {
        float gust = 0.55 + 0.45 * sin(t * 3.4);
        alpha = (GustLayer(screen, t, 29.0) + GustLayer(screen + float2(0.0, 0.14), t * 1.47, 81.0) * 0.64) * gust;
    } else {
        alpha = RainLayer(screen, t, 31.0, 0.81, 0.27, 15.0) * 0.44
              + RainLayer(screen, t, 19.0, 1.26, 0.32, 48.0) * 0.64
              + RainLayer(screen, t, 13.0, 1.61, 0.38, 62.0) * 0.72;
    }
    return float4(StreakTint.rgb, saturate(alpha * StreakTint.a * input.Opacity));
}
