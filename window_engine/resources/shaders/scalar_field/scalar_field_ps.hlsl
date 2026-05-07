Texture2D<float> scalar_field : register(t0);
SamplerState scalar_sampler : register(s0);

cbuffer DebugParams : register(b0)
{
    float display_min;
    float display_max;
    float inv_range;
    uint flags;

    float offset_x;
    float offset_y;
    float zoom;
    float pad0;
};

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float safe_zoom = max(zoom, 0.0001f);

    float2 field_uv =
        (input.uv - float2(0.5f, 0.5f)) / safe_zoom
        + float2(0.5f, 0.5f)
        + float2(offset_x, offset_y);

    // First-pass behavior: clamp at edges.
    // Later, use wrap sampling if you want infinite-feeling pan.
    field_uv = saturate(field_uv);

    float v = scalar_field.Sample(scalar_sampler, field_uv);

    bool normalize_for_display = (flags & 1u) != 0u;

    float out_v = v;

    if (normalize_for_display)
    {
        out_v = saturate((v - display_min) * inv_range);
    }

    return float4(out_v, out_v, out_v, 1.0f);
}