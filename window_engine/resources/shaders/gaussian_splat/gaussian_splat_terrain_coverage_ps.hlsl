// shaders/gaussian_splat/gaussian_splat_terrain_coverage_ps.hlsl
//
// Pixel shader for the GaussianSplatTerrainCoverageDebug program.
//
// VS now emits oriented tangent-plane patches (gaussian_splat_terrain_
// coverage_vs.hlsl).  uv is in [-1, +1] along each axis, with corner pixels
// reaching r = √2.
//
// Two orthogonal axes of behaviour:
//
//   COVERAGE MODE  — what we do with the kernel's coverage value:
//     0 TransparentBlend  — alpha-blend with coverage as alpha (reference)
//     1 CoverageDiscard   — discard if coverage < threshold; opaque inside
//     2 DitheredCoverage  — stable per-pixel hash vs coverage; opaque kept
//     3 OpaqueDisc        — legacy: equivalent to HardDisc + CoverageDiscard
//
//   KERNEL MODE    — how coverage is computed from r_norm = length(uv):
//     0 Gaussian        — exp(-r_norm² * gaussian_falloff)
//     1 SmoothDisc      — 1 - smoothstep(inner_radius, outer_radius, r_norm)
//     2 PolynomialDisc  — saturate(1 - r_norm²)²
//     3 HardDisc        — r_norm ≤ 1 ? 1 : 0
//
// Debug views (debug_view field of coverage_params2.y) override the
// normal colour output for verifying patch orientation:
//   0 Off          — colour unchanged
//   1 PatchUV      — (uv.x*0.5+0.5, uv.y*0.5+0.5, 0.5)
//   2 Normal       — (n.xyz * 0.5 + 0.5)  (world-space)
//   3 TangentSign  — sign(uv.x) / sign(uv.y) tint
//   4 Coverage     — grayscale of the kernel coverage value

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
    float4   coverage_params2;  // min_screen_radius_px, debug_view, _, _
};

struct PSInput
{
    float4 position     : SV_POSITION;
    float3 color        : COLOR;
    float  opacity      : OPACITY;
    float2 uv           : TEXCOORD0;
    float3 normal_world : NORMAL;
    float2 sign_uv      : TEXCOORD1;
};

// Tangent-plane VS emits uv ∈ [-1, +1]; corner of the quad reaches r = √2.
// The kernel's natural support edge is r_norm = 1.0 (or `outer_radius` for
// SmoothDisc), so QUAD_EXTENT = 1.0 makes r_norm = length(uv).
static const float QUAD_EXTENT = 1.0f;

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
    // Gaussian.  coverage = exp(-r_norm² * gaussian_falloff).  At default
    // gaussian_falloff = 4.0 this gives ~0.018 at r_norm = 1 and 1.0 at
    // the centre, similar tail to the legacy 3σ Gaussian.
    return exp(-r_norm * r_norm * gaussian_falloff);
}

float4 main(PSInput input) : SV_TARGET
{
    const float r2_uv = dot(input.uv, input.uv);
    const float r_norm = sqrt(r2_uv) / QUAD_EXTENT;

    const uint  coverage_mode    = (uint)coverage_params0.x;
    const float threshold        = coverage_params0.y;
    const float opacity_scale    = coverage_params0.z;
    const uint  kernel_mode      = (uint)coverage_params0.w;
    const float inner_radius     = coverage_params1.y;
    const float outer_radius     = max(coverage_params1.z, 1e-4f);
    const float gaussian_falloff = max(coverage_params1.w, 1e-4f);
    const uint  debug_view       = (uint)coverage_params2.y;

    const float coverage = eval_kernel(
        kernel_mode, r2_uv, r_norm,
        gaussian_falloff, inner_radius, outer_radius);

    // Zero-coverage early-out (works for all kernels uniformly).
    if (!(coverage > 0.0f))
        discard;

    // ── Coverage mode decides pass/fail and final alpha ──
    bool   is_opaque = true;
    float  out_alpha = 1.0f;

    if (coverage_mode == 0u)
    {
        // TransparentBlend.
        is_opaque = false;
        out_alpha = saturate(input.opacity * coverage * opacity_scale);
    }
    else if (coverage_mode == 1u)
    {
        if (coverage < threshold)
            discard;
    }
    else if (coverage_mode == 2u)
    {
        float keep_prob = saturate(input.opacity * coverage);
        float h = hash_pixel(input.position.xy);
        if (h > keep_prob)
            discard;
    }
    else
    {
        // OpaqueDisc legacy.
        if (r2_uv > 1.0f)
            discard;
    }

    // ── Debug visualisation overrides ──
    // When non-Off we replace the colour output with the chosen visualisation.
    // The pass/fail decision above is preserved so geometry footprint is
    // still legible.
    float3 out_color = input.color;

    if (debug_view == 1u)
    {
        // PatchUV: remap uv from [-1, 1] to [0, 1] for the R and G channels.
        out_color = float3(
            input.uv.x * 0.5f + 0.5f,
            input.uv.y * 0.5f + 0.5f,
            0.5f);
    }
    else if (debug_view == 2u)
    {
        // World-space normal as colour.
        out_color = input.normal_world * 0.5f + 0.5f;
    }
    else if (debug_view == 3u)
    {
        // Tangent-sign quadrant tint — each of the four quad corners gets
        // a distinct colour, which makes patch orientation obvious.
        float su = input.sign_uv.x >= 0.0f ? 1.0f : 0.0f;
        float sv = input.sign_uv.y >= 0.0f ? 1.0f : 0.0f;
        out_color = float3(su, sv, 0.5f);
    }
    else if (debug_view == 4u)
    {
        out_color = float3(coverage, coverage, coverage);
    }

    if (is_opaque)
        return float4(out_color, 1.0f);
    else
        return float4(out_color, out_alpha);
}
