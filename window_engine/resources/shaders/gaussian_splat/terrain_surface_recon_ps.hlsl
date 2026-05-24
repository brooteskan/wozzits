// shaders/gaussian_splat/terrain_surface_recon_ps.hlsl
//
// Pixel shader for the terrain surface reconstruction renderer.
//
// Receives interpolated normal, color, weight, and world position
// from the vertex shader.  Applies simple directional lighting or
// outputs one of several debug views.
//
// Root constants (24 dwords — shared with VS):
//   view_proj  [16]   row-major 4×4
//   params0    [4]    light_dir.xyz, debug_mode
//   params1    [4]    ambient, diffuse_strength, height_min, height_max
//
// Debug modes:
//   0  Lit surface (default)
//   1  Height (grayscale, normalized to [height_min, height_max])
//   2  Reconstructed normal
//   3  Accumulated weight (grayscale, saturated to [0,1])
//   4  Albedo (unlit color)

cbuffer Constants : register(b0)
{
    float4x4 view_proj;
    float4   params0;   // light_dir.xyz, debug_mode
    float4   params1;   // ambient, diffuse_strength, height_min, height_max
};

struct PSInput
{
    float4 sv_position : SV_POSITION;
    float3 normal      : NORMAL;
    float3 color       : COLOR0;
    float  weight      : TEXCOORD0;
    float3 world_pos   : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    const uint debug_mode = (uint)params0.w;

    // ── Debug views ──
    if (debug_mode == 1u)
    {
        // Height visualisation (grayscale, normalized).
        float h_min = params1.z;
        float h_max = params1.w;
        float range = max(h_max - h_min, 1e-6f);
        float t = saturate((input.world_pos.y - h_min) / range);
        return float4(t, t, t, 1.0f);
    }
    else if (debug_mode == 2u)
    {
        // Reconstructed normal visualisation.
        float3 n = normalize(input.normal);
        return float4(n * 0.5f + 0.5f, 1.0f);
    }
    else if (debug_mode == 3u)
    {
        // Accumulated weight visualisation.
        float w = saturate(input.weight);
        return float4(w, w, w, 1.0f);
    }
    else if (debug_mode == 4u)
    {
        // Albedo (unlit).
        return float4(input.color, 1.0f);
    }

    // ── Lit surface (debug_mode == 0) ──
    float3 light_dir = params0.xyz;
    float  ambient   = params1.x;
    float  diffuse   = params1.y;

    float3 n = normalize(input.normal);
    float  ndotl = saturate(dot(n, light_dir));
    float3 lit = input.color * (ambient + diffuse * ndotl);

    return float4(lit, 1.0f);
}
