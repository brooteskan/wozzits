cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4 style_color;
    float4 style_params;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float value : TEXCOORD0;
};

float3 heat_color(float t)
{
    float3 cool = float3(0.05f, 0.18f, 0.55f);
    float3 mid = float3(0.12f, 0.82f, 0.72f);
    float3 hot = float3(1.0f, 0.86f, 0.18f);
    float3 high = float3(0.95f, 0.10f, 0.05f);

    float3 a = lerp(cool, mid, saturate(t * 2.0f));
    float3 b = lerp(hot, high, saturate((t - 0.5f) * 2.0f));
    return lerp(a, b, step(0.5f, t));
}

float3 grayscale_color(float t)
{
    return float3(t, t, t);
}

float3 diverging_color(float t)
{
    float3 low = float3(0.10f, 0.28f, 0.85f);
    float3 mid = float3(0.92f, 0.94f, 0.92f);
    float3 high = float3(0.85f, 0.16f, 0.10f);

    float3 a = lerp(low, mid, saturate(t * 2.0f));
    float3 b = lerp(mid, high, saturate((t - 0.5f) * 2.0f));
    return lerp(a, b, step(0.5f, t));
}

float3 palette_color(float t, uint palette)
{
    if (palette == 1u) {
        return grayscale_color(t);
    }
    if (palette == 2u) {
        return diverging_color(t);
    }
    return heat_color(t);
}

float4 main(PSInput input) : SV_TARGET
{
    uint palette = (uint)round(max(style_params.x, 0.0f));
    float min_value = style_params.y;
    float max_value = style_params.z;
    float gamma = max(style_params.w, 0.0001f);
    float range = max(max_value - min_value, 0.0001f);
    float t = saturate((input.value - min_value) / range);
    t = pow(t, gamma);

    float3 n = normalize(input.normal);
    float facing = saturate(n.y * 0.35f + 0.65f);
    float3 ramp = palette_color(t, palette);
    float3 color = ramp * facing;
    return float4(color, style_color.a);
}
