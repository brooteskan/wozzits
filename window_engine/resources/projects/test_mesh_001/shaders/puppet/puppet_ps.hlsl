
cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;
    float4 xform_row1;
    float4 part_tint;
    float4 part_screen_tint;
};

Texture2D<float4> atlas   : register(t2, space2);
SamplerState      atlas_s : register(s0, space2);

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 tex = atlas.SampleLevel(atlas_s, input.uv, 0.0f);
    float3 rgb = tex.rgb * part_tint.rgb;
    rgb = rgb + part_screen_tint.rgb * (tex.a - rgb);
    float opacity = xform_row0.w;
    return float4(rgb * opacity, tex.a * opacity);
}
