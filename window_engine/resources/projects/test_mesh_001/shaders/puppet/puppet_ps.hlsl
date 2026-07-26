
cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;
    float4 xform_row1;
    float4 part_tint;
    float4 part_screen_tint;
    float4 part_mask;
    uint4  part_pull;
};

Texture2D<float4> atlas   : register(t2, space2);
Texture2D<float4> mask    : register(t3, space2);
SamplerState      atlas_s : register(s0, space2);

struct PSIn
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float2 mask_uv  : TEXCOORD1;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 tex = atlas.SampleLevel(atlas_s, input.uv, 0.0f);
    float3 rgb = tex.rgb * part_tint.rgb;
    rgb = rgb + part_screen_tint.rgb * (tex.a - rgb);
    float coverage = mask.SampleLevel(atlas_s, input.mask_uv, 0.0f).a;
    float covered  = step(part_mask.x, coverage);
    float keep     = lerp(covered, 1.0f - covered, part_mask.y);
    float scale = xform_row0.w * keep;
    return float4(rgb * scale, tex.a * scale);
}
