// shaders/gaussian_splat/gaussian_splat_terrain_field_resolve_ps.hlsl
//
// Fullscreen resolve pixel shader for the terrain field-blend renderer.
//
// Reads the two accumulation render targets:
//   t0 (RGBA16F): rgb = sum(color * weight),  a = sum(weight)
//   t1 (RGBA16F): xyz = sum(normal * weight), w = unused
//
// Resolves the weighted average, applies simple directional lighting,
// and outputs to the backbuffer.
//
// cbuffer at b0 carries resolve parameters:
//   resolve_params0: ambient, diffuse_strength, _, _
//   resolve_params1: light_dir.xyz, debug_mode

cbuffer ResolveParams : register(b0)
{
    // x = ambient, y = diffuse_strength, z = _, w = _
    float4 resolve_params0;
    // xyz = light direction (world-space, normalized by CPU), w = debug_mode
    float4 resolve_params1;
};

Texture2D<float4> t_accum_color  : register(t0);
Texture2D<float4> t_accum_normal : register(t1);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    const int2 px = int2(input.position.xy);

    const float4 accum_c = t_accum_color.Load(int3(px, 0));
    const float4 accum_n = t_accum_normal.Load(int3(px, 0));

    const float sum_w = accum_c.a;

    // No coverage — keep background.
    if (sum_w <= 1e-6f)
        discard;

    const float3 color  = accum_c.rgb / sum_w;
    const float3 raw_n  = accum_n.xyz / sum_w;

    // Normalize the blended normal (may be zero if normals cancelled).
    const float n_len = length(raw_n);
    const float3 normal = (n_len > 1e-6f) ? (raw_n / n_len) : float3(0.0f, 1.0f, 0.0f);

    const float ambient          = resolve_params0.x;
    const float diffuse_strength = resolve_params0.y;
    const float3 light_dir       = resolve_params1.xyz;
    const uint   debug_mode      = (uint)resolve_params1.w;

    // ── Debug views ──
    if (debug_mode == 1u)
    {
        // Accumulated weight visualisation (grayscale, clamped to [0,1]).
        float v = saturate(sum_w);
        return float4(v, v, v, 1.0f);
    }
    else if (debug_mode == 2u)
    {
        // Resolved normal visualisation.
        return float4(normal * 0.5f + 0.5f, 1.0f);
    }
    else if (debug_mode == 3u)
    {
        // Resolved albedo (no lighting).
        return float4(color, 1.0f);
    }

    // ── Simple directional lighting ──
    const float ndotl = saturate(dot(normal, light_dir));
    const float3 lit = color * (ambient + diffuse_strength * ndotl);

    return float4(lit, 1.0f);
}
