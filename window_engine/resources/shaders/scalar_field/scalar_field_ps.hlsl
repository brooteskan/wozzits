Texture2D<float> scalar_field : register(t0);
SamplerState scalar_sampler : register(s0);

cbuffer ScalarFieldDebugParams : register(b0)
{
    float display_min;
    float display_max;
    float inv_range;
    uint display_flags;
};

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float v = scalar_field.Sample(scalar_sampler, input.uv);

    bool normalize_for_display = (display_flags & 1u) != 0u;

    float out_v = v;

    if (normalize_for_display)
    {
        out_v = saturate((v - display_min) * inv_range);
    }

    return float4(out_v, out_v, out_v, 1.0f);
}