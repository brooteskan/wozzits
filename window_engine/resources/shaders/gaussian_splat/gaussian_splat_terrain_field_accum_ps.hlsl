// shaders/gaussian_splat/gaussian_splat_terrain_field_accum_ps.hlsl
//
// Accumulation pixel shader for the terrain field-blend renderer.
//
// Paired with the existing terrain coverage VS (same VSOutput, same
// cbuffer layout).  Each fragment outputs weighted contributions to
// two additive render targets:
//
//   RT0 (RGBA16F): rgb = color * weight,  a = weight
//   RT1 (RGBA16F): xyz = normal * weight, w = 0
//
// The fullscreen resolve pass divides by accumulated weight to
// reconstruct the final surface color and normal.

cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4   viewport_and_size;
    float4   reserved_36_to_39;
    float4   reserved_40_to_43;
    float4   reserved_44_to_47;
    float4   coverage_params0;  // mode, threshold, opacity_scale, kernel_mode
    float4   coverage_params1;  // radius_scale, inner_r, outer_r, gaussian_falloff
    float4   coverage_params2;  // min_screen_radius_px, debug_view, weight_scale, _
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

struct PSOutput
{
    float4 weighted_color  : SV_TARGET0;
    float4 weighted_normal : SV_TARGET1;
};

static const float QUAD_EXTENT = 1.0f;

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
    // Gaussian
    return exp(-r_norm * r_norm * gaussian_falloff);
}

PSOutput main(PSInput input)
{
    const float r2_uv = dot(input.uv, input.uv);
    const float r_norm = sqrt(r2_uv) / QUAD_EXTENT;

    const uint  kernel_mode      = (uint)coverage_params0.w;
    const float inner_radius     = coverage_params1.y;
    const float outer_radius     = max(coverage_params1.z, 1e-4f);
    const float gaussian_falloff = max(coverage_params1.w, 1e-4f);
    const float weight_scale     = max(coverage_params2.z, 1e-4f);

    const float coverage = eval_kernel(
        kernel_mode, r2_uv, r_norm,
        gaussian_falloff, inner_radius, outer_radius);

    if (coverage <= 0.0f)
        discard;

    // Weight = kernel coverage * splat opacity * user-tunable density scale.
    const float w = coverage * input.opacity * weight_scale;

    PSOutput output;
    output.weighted_color  = float4(input.color * w, w);
    output.weighted_normal = float4(input.normal_world * w, 0.0f);
    return output;
}
