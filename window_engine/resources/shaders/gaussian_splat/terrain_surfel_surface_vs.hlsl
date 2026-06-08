// shaders/gaussian_splat/terrain_surfel_surface_vs.hlsl
//
// IA-based terrain surfel vertex shader.
//
// Tangent-plane patch expansion for terrain coverage surfels rendered
// through the vertex-instancing path (TerrainSurfelSurface PSO).
//
// Replaces the camera-facing covariance projection from
// gaussian_splat_debug_vs.hlsl with oriented patches that lie in the
// terrain surface's tangent plane.
//
// The splat quaternion maps local axes as:
//   X (scale.x) → tangent U
//   Y (scale.y) → surface normal  (ignored for expansion — flat patch)
//   Z (scale.z) → tangent V
//
// (Set up this way by gaussian_splat_terrain_surface_compiler.cpp.)

cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;

    // x = viewport width
    // y = viewport height
    // z = radius scale multiplier (1.0 = use scale values as-is)
    // w = unused
    float4 viewport_and_size;
};

struct VSInput
{
    float3 position : POSITION;
    float  opacity  : OPACITY;
    float3 scale    : SCALE;
    float4 rotation : ROTATION;  // x,y,z,w
    float3 color    : COLOR;

    uint vertex_id   : SV_VertexID;
    uint instance_id : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float  opacity  : OPACITY;
    float2 uv       : TEXCOORD0;
};

float4 safe_normalize_quat(float4 q)
{
    float len_sq = dot(q, q);
    if (len_sq <= 0.0000001f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    return q * rsqrt(len_sq);
}

float3 rotate_by_quat(float3 v, float4 q)
{
    float3 qv = q.xyz;
    float  qw = q.w;
    float3 t  = 2.0f * cross(qv, v);
    return v + qw * t + cross(qv, t);
}

VSOutput main(VSInput input)
{
    // Quad corner in [-1, +1] along each tangent axis.
    float2 corners[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 corner = corners[input.vertex_id & 3];

    // Build the tangent frame from the splat quaternion.
    const float4 q  = safe_normalize_quat(input.rotation);
    const float3 sc = max(input.scale, float3(0.000001f, 0.000001f, 0.000001f));

    const float radius_scale = max(viewport_and_size.z, 0.0001f);

    // Tangent-plane axes — Y (normal) is intentionally ignored for
    // expansion; we render a flat patch, not a thin ellipsoid.
    const float3 tangent_u = rotate_by_quat(
        float3(sc.x * radius_scale, 0.0f, 0.0f), q);
    const float3 tangent_v = rotate_by_quat(
        float3(0.0f, 0.0f, sc.z * radius_scale), q);

    // Expand patch in cloud-local space.  The world matrix carries
    // everything into world space.
    const float3 local_pos = input.position
        + corner.x * tangent_u
        + corner.y * tangent_v;

    const float4 world_pos = mul(world, float4(local_pos, 1.0f));
    const float4 clip_pos  = mul(view_proj, world_pos);

    VSOutput output;
    output.position = clip_pos;
    output.color    = input.color;
    output.opacity  = input.opacity;
    output.uv       = corner;   // [-1, +1] at corners
    return output;
}
