// shaders/gaussian_splat/gaussian_splat_terrain_coverage_vs.hlsl
//
// Vertex shader for the GaussianSplatTerrainCoverageDebug program.
//
// Geometry is identical to gaussian_splat_pull_debug_vs.hlsl — we still
// project each splat's oriented ellipsoid into a screen-aligned quad.
// The difference lives in the pixel shader, which reads `coverage_params`
// from the same cbuffer and chooses between transparent blending, hard
// coverage cutoff, dithered coverage, or a hard unit-disc baseline.
//
// Root signature reserves 52 dwords at b0 with ShaderVisibility::All so
// VS and PS read the same cbuffer.  This VS ignores `coverage_params` and
// `lod_params0/1` (those are for the sibling NeighborhoodColorBlend path —
// they're allocated in the constants array but unread here).

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
    // constants array.  This VS doesn't read them; the PS reads
    // coverage_params.
    float4 reserved_36_to_39;
    float4 reserved_40_to_43;
    float4 reserved_44_to_47;

    // coverage_mode, coverage_threshold, coverage_opacity_scale, pad
    float4 coverage_params;
};

struct Splat
{
    float3 position;                    // offset  0
    float  opacity;                     // offset 12
    float3 scale;                       // offset 16
    float  pad0;                        // offset 28
    float4 rotation;                    // offset 32
    float3 color;                       // offset 48
    uint   lod_color_confidence_rgba8;  // offset 60
};

StructuredBuffer<Splat> g_splats         : register(t0);
StructuredBuffer<uint>  g_sorted_indices  : register(t1);

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

VSOutput main(uint vertex_id   : SV_VertexID,
              uint instance_id : SV_InstanceID)
{
    uint splat_index = g_sorted_indices[instance_id];
    Splat s = g_splats[splat_index];

    float2 corners[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 corner = corners[vertex_id & 3];

    float4 q  = safe_normalize_quat(s.rotation);
    float3 sc = max(s.scale, float3(0.000001f, 0.000001f, 0.000001f));

    float debug_scale = viewport_and_size.z;

    float3 axis_x = rotate_by_quat(float3(sc.x * debug_scale, 0.0f, 0.0f), q);
    float3 axis_y = rotate_by_quat(float3(0.0f, sc.y * debug_scale, 0.0f), q);
    float3 axis_z = rotate_by_quat(float3(0.0f, 0.0f, sc.z * debug_scale), q);

    float4 world_center = mul(world, float4(s.position, 1.0f));
    float4 clip_center  = mul(view_proj, world_center);

    float2 center_ndc = clip_center.xy / clip_center.w;

    float2 x_ndc = project_point_to_ndc(s.position + axis_x) - center_ndc;
    float2 y_ndc = project_point_to_ndc(s.position + axis_y) - center_ndc;
    float2 z_ndc = project_point_to_ndc(s.position + axis_z) - center_ndc;

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
