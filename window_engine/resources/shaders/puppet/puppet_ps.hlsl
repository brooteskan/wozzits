// resources/shaders/puppet/puppet_ps.hlsl
//
// An Inochi2D puppet Part's pixel shader: sample the Part's atlas page at the
// pulled UV and modulate alpha by the Part's opacity. S2 renders EVERY Part
// through one AlphaBlend program; the Part's authored blend mode is carried on
// ResidentPuppetPart but not yet consumed at draw -- per-Part Multiply/Screen
// program variants (wz::rhi::BlendMode gained them in #272) are a deferred
// follow-up. Destination-reading blends (Overlay, SoftLight, ...) and stencil
// masks (ClipToLower/SliceFromLower) are later seams (S5 masks, S6 composite).
//
// The atlas is bound at t2 of space 2 (after the two mesh-pull SRVs, matching the
// overlay layout); the sampler is the layout's static clamp sampler at s0. The
// atlas texture is display-referred (sRGB), sampled and output as-is here.

cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;  // opacity in .w
    float4 xform_row1;
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
    float4 tex     = atlas.SampleLevel(atlas_s, input.uv, 0.0f);
    float  opacity = xform_row0.w;
    return float4(tex.rgb, tex.a * opacity);
}
