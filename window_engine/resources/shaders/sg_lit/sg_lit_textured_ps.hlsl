// resources/shaders/sg_lit/sg_lit_textured_ps.hlsl
//
// Textured variant of sg_lit_ps: the SAME SG-lit BRDF, but albedo comes from a
// MATERIAL TEXTURE instead of the hardcoded float3(0.62, 0.62, 0.65). That
// texture is what the material compositor writes -- a base colour plus placed
// art (e.g. an Inochi puppet) -- so the mesh shows the composite, correctly lit.
//
// The BRDF is no longer copied here: it lives in sg_lighting_common.hlsl, wired
// as this program's first source dep (#289). Only what actually differs between
// the variants stays -- the material bindings, the UV derivation, and the albedo
// fetch in main.

Texture2D    material_albedo  : register(t5, space2);
SamplerState material_sampler : register(s0, space2);

// Equirectangular (lat/long) UV from a unit object-space normal: u wraps around
// the equator, v runs pole to pole. This is the SPHERE's material space -- the
// compositor places art in exactly these coordinates, so "move the puppet on the
// sphere" is a UV offset. The u seam (atan2 wrapping at +/-pi) is a hairline the
// linear sampler blends across; with no mip chain there is no derivative
// artifact there.
//
// This is a fallback for geometry with no texture coordinates of its own, which
// is what the lit sphere is: its icosphere carries has_uv0 = false, and an
// icosahedron has no non-degenerate unwrap. A mesh that DOES carry UVs should
// pull them through pulled_mesh_uvs (#290) and pass them down instead -- on
// anything but a sphere this mapping is meaningless.
float2 equirect_uv(float3 n)
{
    float u = atan2(n.z, n.x) / (2.0f * PI) + 0.5f;
    float v = acos(clamp(n.y, -1.0f, 1.0f)) / PI;
    return float2(u, v);
}

struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;
    float3 world_nrm : NORMAL;
    float3 obj_nrm   : OBJECTNORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.world_nrm);
    float3 v = normalize(camera_and_diameter.xyz - input.world_pos);
    // The program rasterizes with CullNone, so the back hemisphere also draws and
    // z-fights the front at the silhouette. Drop back-facing fragments here,
    // winding-independently, so only the front hemisphere shades.
    if (dot(n, v) <= 0.0f) {
        discard;
    }

    // Surface: albedo from the composited material texture; the rest still fixed
    // (authoring roughness/metalness as tail constants remains the #259 follow-up).
    const float2 uv        = equirect_uv(normalize(input.obj_nrm));
    const float3 albedo    = material_albedo.Sample(material_sampler, uv).rgb;
    const float  roughness = 0.35f;
    const float  metalness = 0.0f;

    return float4(sg_shade(n, v, albedo, roughness, metalness), 1.0f);
}
