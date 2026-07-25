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
// #276) and its opacity.
//
// S2 renders EVERY Part through one program; the Part's authored blend mode is
// carried on ResidentPuppetPart but not yet consumed at draw -- per-Part
// Multiply/Screen program variants are a deferred follow-up (#274). Destination-
// reading blends (Overlay, SoftLight, ...) and stencil masks (ClipToLower/
// SliceFromLower) are later seams (S5 masks, S6 composite).
//
// The atlas is bound at t2 of space 2 (after the two mesh-pull SRVs, matching the
// overlay layout); the sampler is the layout's static clamp sampler at s0. The
// atlas texture is display-referred (sRGB), sampled and output as-is here --
// Inochi composites in that space, so we match it rather than linearising.

cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;       // opacity in .w
    float4 xform_row1;
    float4 part_tint;        // rgb = multiply tint  (identity 1,1,1)
    float4 part_screen_tint; // rgb = screen tint    (identity 0,0,0)
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
    // Premultiplied: rgb is already scaled by a.
    float4 tex = atlas.SampleLevel(atlas_s, input.uv, 0.0f);

    // Multiply tint. Scaling a premultiplied colour is the same operation as
    // scaling the straight one, so this needs no adjustment.
    float3 rgb = tex.rgb * part_tint.rgb;

    // Screen tint. In straight alpha this is C + s*(1 - C); premultiplying both
    // sides by a gives rgb + s*(a - rgb), which keeps the result premultiplied
    // without ever dividing by a (and leaves fully transparent texels at zero).
    rgb = rgb + part_screen_tint.rgb * (tex.a - rgb);

    // Opacity scales colour and coverage together, preserving premultiplication.
    float opacity = xform_row0.w;
    return float4(rgb * opacity, tex.a * opacity);
}
