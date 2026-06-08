// shaders/gaussian_splat/terrain_surfel_surface_ps.hlsl
//
// Pixel shader for the TerrainSurfelSurface program.
//
// Soft coverage clipping.  The VS emits uv in [-1, +1] so corner pixels
// reach r = sqrt(2).  Pixels outside the unit disc are discarded; inside,
// alpha fades toward the edge so far terrain reads as coverage instead of
// a stack of opaque discs.

struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float  opacity  : OPACITY;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float r2 = dot(input.uv, input.uv);

    if (r2 > 1.0f)
        discard;

    float edge = 1.0f - smoothstep(0.55f, 1.0f, sqrt(r2));
    float alpha = saturate(input.opacity * edge * 0.45f);
    return float4(saturate(input.color), alpha);
}
