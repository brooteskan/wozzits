// resources/shaders/overlay/overlay_ps.hlsl
//
// A 2D overlay's pixel shader: sample the sprite texture and modulate its alpha
// by the authored overlay alpha. The renderable's program uses AlphaBlend, so
// the returned alpha is the blend weight over the scene beneath.
//
// overlay_texture is the imported-image Texture asset bound at the OverlayTexture
// semantic (t2 by layout row order, after the two mesh-pull SRVs); overlay_sampler
// is the layout's static sampler (s0). The texture carries its own colour space
// as metadata (seam 1) -- an overlay sprite is display-referred (sRGB), sampled
// and output as-is here.

cbuffer OverlayBlock : register(b0, space2)
{
    float4 rect;
    float4 params;   // x = alpha
};

Texture2D<float4> overlay_texture : register(t2, space2);
SamplerState      overlay_sampler : register(s0, space2);

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 tex = overlay_texture.SampleLevel(overlay_sampler, input.uv, 0.0f);
    return float4(tex.rgb, tex.a * params.x);
}
