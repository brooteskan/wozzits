// resources/shaders/clipmap/clipmap_ps.hlsl
//
// Geometry-clipmap landscape pixel shader (issue #198 slice 3b). Simple
// surface shading with a height-band debug tint so the displaced terrain is
// visible AND inspectable (#196 asks only for "visible and inspectable" — kept
// deliberately simple and correct over fancy). No textures sampled here; the
// VS already displaced + computed a finite-difference normal.
//
// Shares the space2 Clipmap cbuffer with the VS so it can read vertical_scale /
// base_height to normalize world height into a debug band. The cbuffer layout
// is documented in clipmap_vs.hlsl and mirrored byte-for-byte by
// ClipmapDrawConstants (rhi_scene_renderer.cpp).

cbuffer Clipmap : register(b0, space2)
{
    float4x4 view_projection;
    float4   lattice_translation_scale;
    float4   world_to_uv;
    float4   texel_and_vertical;  // xy = texel world size, z = vscale, w = base
    float4   texel_dims;
};

struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : TEXCOORD0;
    float3 normal    : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);

    // Lambertian-ish shade from a fixed key light, lifted by ambient so cavities
    // are not pure black.
    float3 light_dir = normalize(float3(0.35f, 0.85f, 0.30f));
    float  ndotl     = saturate(dot(n, light_dir));
    float  shade     = 0.30f + 0.70f * ndotl;

    // Height-band debug tint: normalize world Y back into roughly [0,1] using
    // the authored vertical scale, then ramp low->high through a simple
    // green/brown/white palette so elevation reads at a glance.
    float vertical_scale = texel_and_vertical.z;
    float base_height    = texel_and_vertical.w;
    float h01 = saturate(
        (input.world_pos.y - base_height) / max(vertical_scale, 1e-3f));

    float3 low  = float3(0.20f, 0.42f, 0.18f);   // vegetated low ground
    float3 mid  = float3(0.52f, 0.42f, 0.28f);   // rocky slope
    float3 high = float3(0.92f, 0.92f, 0.95f);   // snow cap
    float3 albedo =
        (h01 < 0.5f)
            ? lerp(low, mid, h01 * 2.0f)
            : lerp(mid, high, (h01 - 0.5f) * 2.0f);

    return float4(albedo * shade, 1.0f);
}
