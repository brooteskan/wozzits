// resources/shaders/star_field/star_field_vs.hlsl
//
// Star-field vertex shader (Seam C-3, issue #266). A SplatPull program: geometry
// is expanded purely from SV_VertexID (6 verts = two triangles per star), the
// same mechanism the gaussian-splat cloud uses. Each star is a camera-facing
// billboard placed on the far celestial sphere; the pixel shader draws it as a
// soft round point, additively.
//
// The cbuffer is StarFieldDrawConstants (binding_layout==5, 40 dwords): world +
// view_proj + camera_and_size (w = authored star_size) + star_params (x =
// authored intensity). `world` is the scene node's transform, so rotating the
// star-field node orients the whole celestial sphere. Both size and intensity
// are live authored dials -- nothing here is a baked magic constant except the
// magnitude->size response curve.
//
// The Star record is the resident 32-byte point layout published by the star
// compiler under "star_catalog" (ResidentStar): direction+solid_angle in one
// 16-byte register, radiance+magnitude in the next.

cbuffer StarView : register(b0, space2)
{
    float4x4 world;               // scene-node transform (orients the sphere)
    float4x4 view_proj;
    float4   camera_and_size;     // xyz = camera world pos, w = star_size
    float4   star_params;         // x = intensity, yzw = spare
};

struct Star
{
    float3 direction;   // unit, celestial (post-import-frame)
    float  solid_angle; // steradians (nominal)
    float3 radiance;    // linear RGB
    float  magnitude;   // apparent visual magnitude
};

StructuredBuffer<Star> g_stars : register(t0, space2);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;   // [-1,1] quad corner, for the round falloff
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    const uint star_index = vertex_id / 6u;
    Star s = g_stars[star_index];

    // Two self-contained triangles (TriangleList) — 6 corners in [-1,1].
    float2 quad[6] = {
        float2(-1.0, -1.0), float2(-1.0, 1.0), float2( 1.0, -1.0),
        float2( 1.0, -1.0), float2(-1.0, 1.0), float2( 1.0,  1.0),
    };
    const float2 corner = quad[vertex_id % 6u];

    // Orient the celestial sphere by the scene node's rotation, then place the
    // star one unit from the eye along its direction and force the vertex to the
    // far plane below (z = w) — the standard skybox-depth trick — so the
    // billboard's screen size is a constant angular footprint regardless of
    // scene scale and the stars sit behind all geometry.
    const float3 dir = normalize(mul((float3x3)world, s.direction));
    const float3 center_ws = camera_and_size.xyz + dir;

    // Camera-facing basis about the view direction to the star.
    const float3 fwd = dir;
    const float3 up0 =
        (abs(fwd.y) < 0.99) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 right = normalize(cross(up0, fwd));
    const float3 up    = cross(fwd, right);

    // Brighter (lower-magnitude) stars get a larger sprite; base from star_size.
    const float size_scale = clamp(1.6 - 0.2 * s.magnitude, 0.35, 3.0);
    const float half_size = max(camera_and_size.w, 1e-4) * 0.01 * size_scale;

    const float3 corner_ws =
        center_ws + corner.x * half_size * right + corner.y * half_size * up;

    float4 clip = mul(view_proj, float4(corner_ws, 1.0));
    clip.z = clip.w;   // far plane

    VSOutput o;
    o.position = clip;
    o.color = s.radiance * star_params.x;   // authored intensity dial
    o.uv = corner;
    return o;
}
