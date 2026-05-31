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
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float facing = saturate(n.y * 0.45f + 0.55f);
    float emissive = max(style_params.x, 0.0f);
    float3 lit = style_color.rgb * (0.35f + 0.65f * facing);
    float3 color = lit + style_color.rgb * emissive;
    return float4(color, style_color.a);
}
