// shaders/gaussian_splat/gaussian_splat_terrain_coverage_ps.hlsl
//
// Pixel shader for the GaussianSplatTerrainCoverageDebug program.
//
// Modes (driven by coverage_params.x):
//
//   0 — TransparentBlend
//       Soft Gaussian alpha blend.  Equivalent to the default 3DGS PS.
//       Suitable when the pipeline state uses AlphaBlend + Depth=Disabled.
//
//   1 — CoverageDiscard
//       Hard cutoff: discard if Gaussian coverage < threshold.  Output is
//       fully opaque inside the kept region.  Suitable with Opaque blend +
//       Depth=TestWrite to make the splat surface participate in the
//       depth buffer.
//
//   2 — DitheredCoverage
//       Per-pixel stable hash compared against Gaussian coverage.  Keeps
//       splats visually soft (dithered edges) while still writing depth.
//       The hash is deterministic on SV_POSITION so the dither does not
//       shimmer frame-to-frame for a static camera.
//
//   3 — OpaqueDisc
//       Hard unit-disc baseline: discard outside r=1 in uv space.  Used to
//       sanity-check the disc geometry independent of Gaussian tuning.
//
// The pipeline state for this program is Opaque blend + Depth::TestWrite,
// which matches modes 1/2/3.  Mode 0 (TransparentBlend) still works under
// that state but without alpha blending the soft edges show as a
// sub-pixel halo — keep it for debug comparisons.

cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4   viewport_and_size;
    float4   reserved_36_to_39;
    float4   reserved_40_to_43;
    float4   reserved_44_to_47;
    // x = mode (0..3, cast from uint), y = threshold, z = opacity_scale, w = pad
    float4   coverage_params;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float  opacity  : OPACITY;
    float2 uv       : TEXCOORD0;
};

// Stable per-pixel hash in [0,1).  Stays constant for a fixed pixel so
// the dither pattern doesn't shimmer frame-to-frame.
float hash_pixel(float2 pix)
{
    // Standard "wang-style" 2D hash.  Cheap and visually acceptable.
    float h = dot(pix, float2(127.1f, 311.7f));
    return frac(sin(h) * 43758.5453f);
}

float4 main(PSInput input) : SV_TARGET
{
    float r2 = dot(input.uv, input.uv);

    // gaussian_radius from the VS = 3.0; r2 > 9 means we're beyond ~3σ
    // and contribution is < 1.1% in any mode.  Cheap early-out.
    if (r2 > 9.0f)
        discard;

    // Gaussian coverage in [0, 1].
    //   r2 = 0 -> 1.0
    //   r2 = 1 -> 0.607
    //   r2 = 4 -> 0.135
    //   r2 = 9 -> 0.011
    float gaussian = exp(-0.5f * r2);

    const uint mode = (uint)coverage_params.x;
    const float threshold     = coverage_params.y;
    const float opacity_scale = coverage_params.z;

    if (mode == 0u)
    {
        // TransparentBlend — original behaviour.
        float alpha = saturate(input.opacity * gaussian * opacity_scale);
        return float4(input.color, alpha);
    }
    else if (mode == 1u)
    {
        // CoverageDiscard.
        if (gaussian < threshold)
            discard;
        return float4(input.color, 1.0f);
    }
    else if (mode == 2u)
    {
        // DitheredCoverage.  Stable per-pixel hash compared against the
        // Gaussian coverage scaled by opacity.  Discarded pixels leave
        // the depth/color buffers untouched; kept pixels write opaque.
        float keep_prob = saturate(input.opacity * gaussian);
        float h = hash_pixel(input.position.xy);
        if (h > keep_prob)
            discard;
        return float4(input.color, 1.0f);
    }
    else
    {
        // OpaqueDisc — hard unit-disc, no Gaussian.
        if (r2 > 1.0f)
            discard;
        return float4(input.color, 1.0f);
    }
}
