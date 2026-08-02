// shaders/gaussian_splat/gaussian_splat_terrain_coverage_vs.hlsl
//
// Tangent-plane patch expansion for terrain coverage splats.
//
// Replaces the camera-facing covariance projection inherited from
// gaussian_splat_pull_debug_vs.hlsl.  For terrain we want each splat to
// expand as an oriented patch lying in the surface tangent plane:
//
//   local axis X (rotated by splat quaternion) = world-space tangent U
//   local axis Z (rotated by splat quaternion) = world-space tangent V
//   local axis Y (rotated by splat quaternion) = world-space surface normal
//                                                (debug-only; not used to
//                                                 expand the patch)
//
// The splat's scale.x / scale.z give the half-extents of the patch along
// U/V; scale.y is the normal-axis thickness and is intentionally ignored
// here (we render a flat patch, not a thin ellipsoid).
//
// Output:
//   uv  ∈ [-1, +1] along each axis (corners at ±√2)
//   normal_world for the debug visualisation
//   sign_uv     for the TangentSign debug view
//
// Pull-debug VS is intentionally unchanged — that one stays the right
// model for general 3DGS / point-cloud rendering.

cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;

    // x = viewport width
    // y = viewport height
    // z = debug scale multiplier
    // w = unused
    float4 viewport_and_size;

    // Reserved slots so the cbuffer layout matches the submit-side
    // constants array (NeighborhoodColorBlend lives in these slots; this
    // VS doesn't read them).
    float4 reserved_36_to_39;
    float4 reserved_40_to_43;
    float4 reserved_44_to_47;

    // [48..51] coverage_params0:
    //   x = coverage_mode, y = threshold, z = opacity_scale, w = kernel_mode
    float4 coverage_params0;
    // [52..55] coverage_params1:
    //   x = radius_scale, y = inner_radius, z = outer_radius, w = gaussian_falloff
    float4 coverage_params1;
    // [56..59] coverage_params2:
    //   x = min_screen_radius_px (currently unused in this VS),
    //   y = debug_view, z/w = pad
    float4 coverage_params2;
};

struct Splat
{
    float3 position;                    // offset  0
    float  opacity;                     // offset 12
    float3 scale;                       // offset 16 — scale.x = tangent_u,
                                        //              scale.y = normal,
                                        //              scale.z = tangent_v
    float  pad0;                        // offset 28
    float4 rotation;                    // offset 32  x,y,z,w (normalised)
    float3 color;                       // offset 48
    uint   lod_color_confidence_rgba8;  // offset 60
};

StructuredBuffer<Splat> g_splats         : register(t0);
StructuredBuffer<uint>  g_sorted_indices  : register(t1);

struct VSOutput
{
    float4 position     : SV_POSITION;
    float3 color        : COLOR;
    float  opacity      : OPACITY;
    float2 uv           : TEXCOORD0;
    float3 normal_world : NORMAL;
    float2 sign_uv      : TEXCOORD1;
};

float4 safe_normalize_quat(float4 q)
{
    float len_sq = dot(q, q);
    // Reject direction: every comparison with NaN is false, so the accept
    // spelling (len_sq <= eps) let a non-finite quaternion straight through
    // the guard and returned NaN from a function named "safe" (issue #316).
    if (!isfinite(len_sq) || !(len_sq > 0.0000001f))
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

VSOutput main(uint vertex_id   : SV_VertexID,
              uint instance_id : SV_InstanceID)
{
    uint splat_index = g_sorted_indices[instance_id];
    Splat s = g_splats[splat_index];

    // Quad corner in [-1, +1] along each axis.
    float2 corners[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 corner = corners[vertex_id & 3];

    // ── Build the tangent frame from the splat quaternion ──
    // The quaternion maps the splat's local frame (X, Y, Z) into the
    // cloud's local space.  X = tangent U, Y = normal, Z = tangent V.
    // (Set up this way by gaussian_splat_terrain_surface_compiler.cpp.)
    const float4 q = safe_normalize_quat(s.rotation);
    const float3 sc = max(s.scale, float3(0.000001f, 0.000001f, 0.000001f));

    const float3 tangent_u_local = rotate_by_quat(float3(sc.x, 0.0f, 0.0f), q);
    const float3 tangent_v_local = rotate_by_quat(float3(0.0f, 0.0f, sc.z), q);
    const float3 normal_local    = rotate_by_quat(float3(0.0f, 1.0f, 0.0f), q);

    // ── Expand patch in cloud-local space ──
    // The world matrix takes care of the cloud's overall transform
    // (rotation/scale/translation), so we add the corner offset to the
    // splat's local position and let `world` carry both into world space.
    const float radius_scale = max(coverage_params1.x, 0.0001f);

    const float3 local_offset =
        corner.x * tangent_u_local * radius_scale +
        corner.y * tangent_v_local * radius_scale;

    const float3 local_pos  = s.position + local_offset;
    const float4 world_pos  = mul(world, float4(local_pos, 1.0f));
    const float4 clip_pos   = mul(view_proj, world_pos);

    // For debug viz only — transform the normal by `world` as a direction
    // (w=0).  Cloud world matrices are translation+rotation in this
    // pipeline so this is sufficient; if non-uniform scale ever enters we
    // would need the inverse-transpose, but the existing terrain pipeline
    // does not produce that.
    const float3 normal_world =
        normalize(mul(world, float4(normal_local, 0.0f)).xyz);

    VSOutput output;
    output.position     = clip_pos;
    output.color        = s.color;
    output.opacity      = s.opacity;
    output.uv           = corner;          // ∈ [-1, +1]² at corners
    output.normal_world = normal_world;
    output.sign_uv      = corner;          // raw sign carrier for debug viz
    return output;
}
