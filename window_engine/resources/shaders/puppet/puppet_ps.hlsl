// resources/shaders/puppet/puppet_ps.hlsl
//
// An Inochi2D puppet Part's pixel shader. It works entirely in PREMULTIPLIED
// alpha (#277): publish_resident_puppet premultiplies each atlas page before
// upload, so the sample arrives premultiplied, every operation below preserves
// that, and the puppet program pairs this with a PremultipliedAlpha PSO
// (ONE, INV_SRC_ALPHA). Premultiplying before the sampler filters is what stops
// transparent Part borders fringing toward black -- doing it here instead would
// be a no-op, since rgb*a with ONE/INV_SRC_ALPHA is algebraically identical to
// straight alpha with SRC_ALPHA/INV_SRC_ALPHA.
//
// On top of that it applies the Part's colour modulation (tint then screen tint,
// #276), its coverage MASK (#275) and its opacity.
//
// Masking: the mask source Part is rendered alone into a target-sized texture in
// a prepass, and its ALPHA is the coverage tested here. Every Part samples the
// mask -- an unmasked one binds a 1x1 white texture with threshold 0, so the
// test passes everywhere and one program set covers both cases. The mask is in
// TARGET space, so the VS hands down the target-space UV rather than this shader
// reconstructing it from SV_POSITION (which would need the viewport pixel-side).
//
// The per-Part blend mode selects among program VARIANTS (#274) rather than
// anything here. Destination-reading blends (Overlay, SoftLight, ...) are a
// later seam and currently draw as Normal.
//
// The atlas is bound at t2 of space 2 (after the two mesh-pull SRVs, matching the
// overlay layout) and the mask at t3; the sampler is the layout's static clamp
// sampler at s0. The atlas texture is display-referred (sRGB), sampled and output
// as-is here -- Inochi composites in that space, so we match it.

cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;       // opacity in .w
    float4 xform_row1;
    float4 part_tint;        // rgb = multiply tint  (identity 1,1,1)
    float4 part_screen_tint; // rgb = screen tint    (identity 0,0,0)
    float4 part_mask;        // x = threshold, y = invert (DodgeMask), zw unused
    uint4  part_pull;        // shared-buffer bases, VS only (#278)
};

Texture2D<float4> atlas   : register(t2, space2);
Texture2D<float4> mask    : register(t3, space2);
SamplerState      atlas_s : register(s0, space2);

struct PSIn
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float2 mask_uv  : TEXCOORD1;   // target-space [0,1]
};

float4 main(PSIn input) : SV_TARGET
{
    // Premultiplied: rgb is already scaled by a.
    float4 tex = atlas.SampleLevel(atlas_s, input.uv, 0.0f);

    // Multiply tint. Scaling a premultiplied colour is the same operation as
    // scaling the straight one, so this needs no adjustment.
    float3 rgb = tex.rgb * part_tint.rgb;

    // Screen tint. In straight alpha this is C + s*(1 - C); premultiplying both
    // sides by a gives rgb + s*(a - rgb), which keeps the result premultiplied
    // without ever dividing by a (and leaves fully transparent texels at zero).
    rgb = rgb + part_screen_tint.rgb * (tex.a - rgb);

    // Coverage mask. The source's alpha is its coverage; Mask keeps the texels
    // the source covers, DodgeMask keeps the ones it does not. A hard test
    // rather than a multiply, because mask_threshold is Inochi's cutoff.
    float coverage = mask.SampleLevel(atlas_s, input.mask_uv, 0.0f).a;
    float covered  = step(part_mask.x, coverage);
    float keep     = lerp(covered, 1.0f - covered, part_mask.y);

    // Opacity scales colour and coverage together, preserving premultiplication;
    // so does the mask, which is why it can be folded into the same factor.
    float scale = xform_row0.w * keep;
    return float4(rgb * scale, tex.a * scale);
}
