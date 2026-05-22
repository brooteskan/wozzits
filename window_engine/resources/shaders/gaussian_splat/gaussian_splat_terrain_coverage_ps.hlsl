// shaders/gaussian_splat/gaussian_splat_terrain_coverage_ps.hlsl
//
// Pixel shader for the GaussianSplatTerrainCoverageDebug program.
//
// Two orthogonal axes of behaviour:
//
//   COVERAGE MODE  — what we do with the kernel's coverage value:
//     0 TransparentBlend  — alpha-blend with coverage as alpha (reference)
//     1 CoverageDiscard   — discard if coverage < threshold; opaque inside
//     2 DitheredCoverage  — stable per-pixel hash vs coverage; opaque kept
//     3 OpaqueDisc        — kept for backward compat; equivalent to
//                           kernel_mode=HardDisc + CoverageDiscard@threshold=0.5
//
//   KERNEL MODE    — how coverage is computed from normalized radius r:
//     0 Gaussian        — exp(-0.5 * r² * gaussian_falloff)
//     1 SmoothDisc      — 1 - smoothstep(inner_radius, outer_radius, r_norm)
//     2 PolynomialDisc  — saturate(1 - r_norm²)²
//     3 HardDisc        — r_norm ≤ 1 ? 1 : 0
//
// The kernel decides the *shape* of the footprint; the coverage mode
// decides the *pass/fail rule*.  Most useful combos for terrain:
//   kernel=SmoothDisc + coverage=CoverageDiscard  → full opaque interior,
//     soft controlled rim, writes depth.  Makes terrain read as connected
//     surface patches rather than isolated discs at close range.
//   kernel=SmoothDisc + coverage=DitheredCoverage → soft visual edge while
//     still depth-writing.
//   kernel=Gaussian   + coverage=TransparentBlend → reference 3DGS look.
//
// uv-space convention: VS scales the quad to ±quad_extent (3.0) along its
// projected ellipse axes; the support boundary in uv-space is r = quad_extent.
// We normalize r_norm = r / quad_extent so kernel parameters live in [0, 1].

cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4   viewport_and_size;
    float4   reserved_36_to_39;
    float4   reserved_40_to_43;
    float4   reserved_44_to_47;
    float4   coverage_params0;  // mode, threshold, opacity_scale, kernel_mode
    float4   coverage_params1;  // radius_scale, inner_radius, outer_radius, gaussian_falloff
    float4   coverage_params2;  // min_screen_radius_px, _, _, _
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float  opacity  : OPACITY;
    float2 uv       : TEXCOORD0;
};

// Quad extent in uv-space (matches the VS).  r_norm = sqrt(uv²) / quad_extent
// maps r=0 at the centre, r=1 at the projected support edge along axes,
// r=√2 at the quad's diagonal corners.
static const float QUAD_EXTENT = 3.0f;

// Stable per-pixel hash in [0,1).  Deterministic on SV_POSITION so the
// dither pattern is fixed for a static camera (no shimmer).
float hash_pixel(float2 pix)
{
    float h = dot(pix, float2(127.1f, 311.7f));
    return frac(sin(h) * 43758.5453f);
}

float eval_kernel(
    uint  kernel_mode,
    float r2_uv,
    float r_norm,
    float gaussian_falloff,
    float inner_radius,
    float outer_radius)
{
    if (kernel_mode == 1u) // SmoothDisc
    {
        return 1.0f - smoothstep(inner_radius, outer_radius, r_norm);
    }
    else if (kernel_mode == 2u) // PolynomialDisc
    {
        float t = saturate(1.0f - r_norm * r_norm);
        return t * t;
    }
    else if (kernel_mode == 3u) // HardDisc
    {
        return r_norm <= 1.0f ? 1.0f : 0.0f;
    }
    // Gaussian (default).  Falloff at gaussian_falloff=1.0 matches the
    // legacy PS (coverage = exp(-r²/2) over r ∈ [0, 3]).
    return exp(-0.5f * r2_uv * gaussian_falloff);
}

float4 main(PSInput input) : SV_TARGET
{
    const float r2_uv = dot(input.uv, input.uv);

    // Cheap early-out: anything beyond the quad's circumscribed circle is
    // outside all kernels' supports.  quad_extent² = 9 at corners ≈ 2 (well
    // beyond 1.0 normalized), so anything past r2_uv > QUAD_EXTENT² is
    // safe to discard outright.  (Corners go to r²≈18; we leave those for
    // kernel-specific discards below.)

    const uint  coverage_mode    = (uint)coverage_params0.x;
    const float threshold        = coverage_params0.y;
    const float opacity_scale    = coverage_params0.z;
    const uint  kernel_mode      = (uint)coverage_params0.w;
    const float inner_radius     = coverage_params1.y;
    const float outer_radius     = max(coverage_params1.z, 1e-4f);
    const float gaussian_falloff = max(coverage_params1.w, 1e-4f);

    const float r_norm = sqrt(r2_uv) / QUAD_EXTENT;

    const float coverage = eval_kernel(
        kernel_mode, r2_uv, r_norm,
        gaussian_falloff, inner_radius, outer_radius);

    // Zero-coverage early-out — same as Gaussian's old `r2 > 9` discard
    // but generalised to all kernels.
    if (coverage <= 0.0f)
        discard;

    if (coverage_mode == 0u)
    {
        // TransparentBlend — kernel-driven alpha.
        float alpha = saturate(input.opacity * coverage * opacity_scale);
        return float4(input.color, alpha);
    }
    else if (coverage_mode == 1u)
    {
        // CoverageDiscard.
        if (coverage < threshold)
            discard;
        return float4(input.color, 1.0f);
    }
    else if (coverage_mode == 2u)
    {
        // DitheredCoverage.  Compare a stable per-pixel hash to a kept
        // probability driven by opacity × coverage.  Discarded pixels
        // leave the depth/colour buffers untouched; kept pixels write
        // opaque.
        float keep_prob = saturate(input.opacity * coverage);
        float h = hash_pixel(input.position.xy);
        if (h > keep_prob)
            discard;
        return float4(input.color, 1.0f);
    }
    else
    {
        // OpaqueDisc (legacy): r ≤ 1 in uv-space corresponds to the
        // projected ellipse interior.  Equivalent to kernel=HardDisc with
        // coverage_mode=CoverageDiscard at threshold=0.5 — kept for the
        // existing UI radio button.
        if (r2_uv > 1.0f)
            discard;
        return float4(input.color, 1.0f);
    }
}
