// resources/shaders/sg_lit/sg_lit_uv_ps.hlsl
//
// UV-mapped variant of sg_lit_textured_ps: the same SG-lit BRDF and the same
// material texture, sampled with the MESH'S OWN texture coordinates (#290)
// rather than an equirectangular UV derived from the object normal.
//
// So "a material texture on a surface" finally means a surface, not a sphere:
// the compositor's output (#285) maps the way the mesh was authored, on a GLB
// import or anything else that carries UVs.
//
// The BRDF comes from sg_lighting_common.hlsl on the shared_source port (#289),
// which also fixes sky_gaussian at t3 and sky_gaussian_points at t4 -- so a
// layout for this program must place those rows at exactly those indices.

Texture2D    material_albedo  : register(t5, space2);
SamplerState material_sampler : register(s0, space2);

struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;
    float3 world_nrm : NORMAL;
    float2 uv        : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.world_nrm);
    float3 v = normalize(camera_and_diameter.xyz - input.world_pos);
    // The program rasterizes with CullNone, so the back hemisphere also draws
    // and z-fights the front at the silhouette. Drop back-facing fragments here,
    // winding-independently, so only the front face shades.
    // Reject direction. This discard is the ONLY thing stopping the back
    // hemisphere z-fighting the front under CullNone, and a degenerate mesh
    // normal makes normalize() return NaN -- which the accept spelling let
    // through, shading a NaN fragment (issue #316, C3-C3).
    if (!(dot(n, v) > 0.0f)) {
        discard;
    }

    const float3 albedo    = material_albedo.Sample(material_sampler, input.uv).rgb;
    const float  roughness = 0.35f;
    const float  metalness = 0.0f;

    return float4(sg_shade(n, v, albedo, roughness, metalness), 1.0f);
}
