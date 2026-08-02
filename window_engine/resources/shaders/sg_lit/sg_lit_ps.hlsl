// resources/shaders/sg_lit/sg_lit_ps.hlsl
//
// SG-lit PBR surface pixel shader (lighting model Seam 3, umbrella #259). The
// BRDF itself lives in sg_lighting_common.hlsl, wired as this program's first
// source dep (#289) -- the shader node is [sg_lighting_common, sg_lit_ps], and
// dx12_shader.cpp concatenates them in that order. Surface params are hardcoded
// for first light; authoring them as tail constants is a follow-up.

struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;
    float3 world_nrm : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.world_nrm);
    float3 v = normalize(camera_and_diameter.xyz - input.world_pos);
    // The program rasterizes with CullNone, so the back hemisphere also draws and
    // z-fights the front at the silhouette (the speckled rim). Real outward
    // normals let us drop back-facing fragments here, winding-independently, so
    // only the front hemisphere shades -> a clean edge.
    // Reject direction. This discard is the ONLY thing stopping the back
    // hemisphere z-fighting the front under CullNone, and a degenerate mesh
    // normal makes normalize() return NaN -- which the accept spelling let
    // through, shading a NaN fragment (issue #316, C3-C3).
    if (!(dot(n, v) > 0.0f)) {
        discard;
    }

    // Hardcoded surface (first light).
    const float3 albedo    = float3(0.62f, 0.62f, 0.65f);
    const float  roughness = 0.35f;
    const float  metalness = 0.0f;

    return float4(sg_shade(n, v, albedo, roughness, metalness), 1.0f);
}
