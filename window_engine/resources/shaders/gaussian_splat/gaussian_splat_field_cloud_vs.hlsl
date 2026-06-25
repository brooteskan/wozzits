// resources/shaders/gaussian_splat/gaussian_splat_field_cloud_vs.hlsl
//
// Scalar-field gaussian-splat-cloud vertex shader (issue #208). Renders a
// resident GaussianSplatCloudData (produced by the "Gaussian splat from field"
// converter) as a camera-facing gaussian-splat cloud on the wozzits-rhi path,
// so the raw heightmap can be inspected as a point/splat cloud alongside the
// geometry-clipmap mesh (#207) for an A/B comparison.
//
// SplatPull binding model, but slotted into the SAME object SRG (space2) that
// RhiSceneRenderer binds for the clipmap landscape — so it rides the existing
// realize path (binding_layout == 3 in render_program_compilers.cpp):
//   b0, space2  36 x float root constants (SplatViewConstants, see below)
//   t0, space2  StructuredBuffer<Splat>  (the resident decoded splat cloud)
//
// NO instancing and NO index buffer: the renderer issues one non-indexed
// DrawInstanced(4 * splat_count, 1, 0, 0). Each group of 4 consecutive vertex
// ids is one splat's camera-facing quad — splat_index = vid / 4, corner =
// vid & 3 — which keeps make_draw_args (no instance_count on GeometryView)
// unchanged. The splat math (screen-space covariance footprint) mirrors
// gaussian_splat_pull_debug_vs.hlsl; only the binding space, the vid → splat
// mapping, and the world/view-projection source differ.
//
// ── SplatViewConstants byte layout (MUST match SplatCloudDrawConstants in
//    src/engine/rendering/rhi_scene_renderer.cpp byte-for-byte) ──────────────
//
//   offset  bytes  field
//   ------  -----  -----------------------------------------------------------
//      0      64   float4x4 world          (column-major, mul(world, p))
//     64      64   float4x4 view_proj      (column-major, mul(view_proj, p))
//    128      16   float4   viewport_and_size
//                            xy = viewport (w,h) px, z = splat_size, w = unused
//   ------
//    144 bytes total = 36 dwords.

cbuffer SplatView : register(b0, space2)
{
    float4x4 world;
    float4x4 view_proj;
    float4   viewport_and_size;  // xy = viewport px, z = splat size, w = unused
};

// Mirror of the resident decoded splat record uploaded by
// publish_resident_gaussian_splat_cloud (64 bytes, offsets pinned by a
// static_assert on the CPU side). The packing here is load-bearing: any field
// reorder/size change on the CPU MUST be reflected here, or the pull shader
// reads garbage. Matches DX12GaussianSplatVertex's stride (the legacy IA path's
// 64-byte vertex), but is fed from a StructuredBuffer instead of the IA.
struct Splat
{
    float3 position;   // offset  0  world-space splat center
    float  opacity;    // offset 12  decoded (sigmoid) in [0,1]
    float3 scale;      // offset 16  decoded (exp) world-space axis sizes
    float  pad0;       // offset 28
    float4 rotation;   // offset 32  x,y,z,w (normalized)
    float3 color;      // offset 48  decoded linear RGB
    uint   pad1;       // offset 60
};

StructuredBuffer<Splat> g_splats : register(t0, space2);

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

float2 project_point_to_ndc(float3 p)
{
    float4 world_pos = mul(world, float4(p, 1.0f));
    float4 clip_pos  = mul(view_proj, world_pos);
    return clip_pos.xy / clip_pos.w;
}

VSOutput main(uint vertex_id : SV_VertexID)
{
    uint  splat_index = vertex_id / 4u;   // 4 verts per splat quad
    Splat s           = g_splats[splat_index];

    float2 corners[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 corner = corners[vertex_id & 3u];

    float4 q  = safe_normalize_quat(s.rotation);
    float3 sc = max(s.scale, float3(0.000001f, 0.000001f, 0.000001f));

    float splat_size = max(viewport_and_size.z, 0.000001f);

    float3 axis_x = rotate_by_quat(float3(sc.x * splat_size, 0.0f, 0.0f), q);
    float3 axis_y = rotate_by_quat(float3(0.0f, sc.y * splat_size, 0.0f), q);
    float3 axis_z = rotate_by_quat(float3(0.0f, 0.0f, sc.z * splat_size), q);

    float4 world_center = mul(world, float4(s.position, 1.0f));
    float4 clip_center  = mul(view_proj, world_center);

    float2 center_ndc = clip_center.xy / clip_center.w;

    float2 x_ndc = project_point_to_ndc(s.position + axis_x) - center_ndc;
    float2 y_ndc = project_point_to_ndc(s.position + axis_y) - center_ndc;
    float2 z_ndc = project_point_to_ndc(s.position + axis_z) - center_ndc;

    // Screen-space 2x2 covariance approximation: C = xx^T + yy^T + zz^T, the
    // projected footprint of the 3D ellipsoid (identical to the pull-debug VS).
    float c00 = x_ndc.x * x_ndc.x + y_ndc.x * y_ndc.x + z_ndc.x * z_ndc.x;
    float c01 = x_ndc.x * x_ndc.y + y_ndc.x * y_ndc.y + z_ndc.x * z_ndc.y;
    float c11 = x_ndc.y * x_ndc.y + y_ndc.y * y_ndc.y + z_ndc.y * z_ndc.y;

    float trace = c00 + c11;
    float diff  = c00 - c11;
    float disc  = sqrt(max(0.0f, 0.25f * diff * diff + c01 * c01));

    float lambda_major = max(0.0f, 0.5f * trace + disc);
    float lambda_minor = max(0.0f, 0.5f * trace - disc);

    float2 major_dir;
    if (abs(c01) > 0.00000001f)
        major_dir = normalize(float2(lambda_major - c11, c01));
    else
        major_dir = (c00 >= c11) ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);

    float2 minor_dir = float2(-major_dir.y, major_dir.x);

    float major_len = sqrt(lambda_major);
    float minor_len = sqrt(lambda_minor);

    // Clamp minimum visible size so tiny/degenerate splats do not vanish.
    float2 viewport  = max(viewport_and_size.xy, float2(1.0f, 1.0f));
    float  min_ndc_x = 2.0f / viewport.x;
    float  min_ndc_y = 2.0f / viewport.y;
    float  min_ndc   = max(min_ndc_x, min_ndc_y);

    major_len = max(major_len, min_ndc);
    minor_len = max(minor_len, min_ndc);

    float2 axis_major_ndc = major_dir * major_len;
    float2 axis_minor_ndc = minor_dir * minor_len;

    float gaussian_radius = 3.0f;

    float2 ndc_offset =
        corner.x * axis_major_ndc * gaussian_radius +
        corner.y * axis_minor_ndc * gaussian_radius;

    float4 clip_pos = clip_center;
    clip_pos.xy    += ndc_offset * clip_center.w;

    VSOutput output;
    output.position = clip_pos;
    output.color    = s.color;
    output.opacity  = s.opacity;
    output.uv       = corner * gaussian_radius;
    return output;
}
