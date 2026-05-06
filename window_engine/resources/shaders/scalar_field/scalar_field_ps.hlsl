Texture2D<float> scalar_field : register(t0);
SamplerState scalar_sampler : register(s0);

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float v = scalar_field.Sample(scalar_sampler, input.uv);
    return float4(v, v, v, 1.0);
}