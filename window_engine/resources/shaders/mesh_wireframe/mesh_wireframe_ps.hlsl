cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4 style_color;
    float4 style_params;
};

float4 main() : SV_TARGET
{
    return float4(style_color.rgb * style_params.x, style_color.a);
}
