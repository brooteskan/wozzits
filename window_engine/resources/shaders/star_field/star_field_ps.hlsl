// resources/shaders/star_field/star_field_ps.hlsl
//
// Star-field pixel shader (Seam C-3, issue #266). Masks the billboard quad to a
// round point and weights the star's radiance by a soft radial falloff. Meant to
// run under additive blend (BlendMode::Additive, src*ONE + dst*ONE): the output
// is pre-weighted by the falloff, so overlapping stars accumulate light and
// nothing occludes.

struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;   // [-1,1] quad corner
};

float4 main(PSInput input) : SV_TARGET
{
    const float d2 = dot(input.uv, input.uv);
    if (d2 > 1.0) {
        discard;   // outside the unit disc
    }

    // Soft core: 1 at center, 0 at the rim, squared for a tighter point.
    float falloff = saturate(1.0 - d2);
    falloff *= falloff;

    return float4(input.color * falloff, falloff);
}
