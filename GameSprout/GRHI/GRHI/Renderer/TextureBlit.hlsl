struct ImageVertex {
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

ImageVertex imageVertex(uint id : SV_VertexID) {
    const float2 uv = float2((id & 1u) * 2u, id & 2u);
    ImageVertex result;
    result.Position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    result.UV = uv;
    return result;
}

// SDL GPU binds fragment sampled textures and samplers in register space 2.
Texture2D image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);

float4 imageFragment(ImageVertex input) : SV_Target0 {
    return image.Sample(imageSampler, input.UV);
}
