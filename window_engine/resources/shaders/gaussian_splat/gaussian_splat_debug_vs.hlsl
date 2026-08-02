cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;

    // x = viewport width
    // y = viewport height
    // z = temporary debug scale multiplier
    // w = unused
    float4 viewport_and_size;
};

struct VSInput
{
    float3 position : POSITION;
    float opacity : OPACITY;
    float3 scale : SCALE;
    float4 rotation : ROTATION; // x,y,z,w
    float3 color : COLOR;

    uint vertex_id : SV_VertexID;
    uint instance_id : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float opacity : OPACITY;
    float2 uv : TEXCOORD0;
};

float4 safe_normalize_quat(float4 q)
{
    float len_sq = dot(q, q);

    // Reject direction: every comparison with NaN is false, so the accept
    // spelling (len_sq <= eps) let a non-finite quaternion straight through
    // the guard and returned NaN from a function named "safe" (issue #316).
    if (!isfinite(len_sq) || !(len_sq > 0.0000001f))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return q * rsqrt(len_sq);
}

float3 rotate_by_quat(float3 v, float4 q)
{
    float3 qv = q.xyz;
    float qw = q.w;

    float3 t = 2.0f * cross(qv, v);
    return v + qw * t + cross(qv, t);
}

float2 project_point_to_ndc(float3 p)
{
    float4 world_pos = mul(world, float4(p, 1.0f));
    float4 clip_pos = mul(view_proj, world_pos);

    return clip_pos.xy / clip_pos.w;
}

VSOutput main(VSInput input)
{
    float2 corners[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 1.0f),
        float2(1.0f, -1.0f),
        float2(1.0f, 1.0f)
    };

    float2 corner = corners[input.vertex_id & 3];

    float4 q = safe_normalize_quat(input.rotation);

    float3 s = max(input.scale, float3(0.000001f, 0.000001f, 0.000001f));

    // Temporary visibility multiplier. This is still not final physical sizing.
    float debug_scale = viewport_and_size.z;

    float3 axis_x = rotate_by_quat(float3(s.x * debug_scale, 0.0f, 0.0f), q);
    float3 axis_y = rotate_by_quat(float3(0.0f, s.y * debug_scale, 0.0f), q);
    float3 axis_z = rotate_by_quat(float3(0.0f, 0.0f, s.z * debug_scale), q);

    float4 world_center = mul(world, float4(input.position, 1.0f));
    float4 clip_center = mul(view_proj, world_center);

    float2 center_ndc = clip_center.xy / clip_center.w;

    float2 x_ndc = project_point_to_ndc(input.position + axis_x) - center_ndc;
    float2 y_ndc = project_point_to_ndc(input.position + axis_y) - center_ndc;
    float2 z_ndc = project_point_to_ndc(input.position + axis_z) - center_ndc;

    // Screen-space 2x2 covariance approximation:
    //
    // C = xx^T + yy^T + zz^T
    //
    // This is the key change from "draw a rotated disk" to
    // "draw the projected footprint of a 3D ellipsoid."
    float c00 =
        x_ndc.x * x_ndc.x +
        y_ndc.x * y_ndc.x +
        z_ndc.x * z_ndc.x;

    float c01 =
        x_ndc.x * x_ndc.y +
        y_ndc.x * y_ndc.y +
        z_ndc.x * z_ndc.y;

    float c11 =
        x_ndc.y * x_ndc.y +
        y_ndc.y * y_ndc.y +
        z_ndc.y * z_ndc.y;

    // Eigen decomposition of symmetric 2x2 matrix:
    //
    // [ c00 c01 ]
    // [ c01 c11 ]
    float trace = c00 + c11;
    float diff = c00 - c11;

    float disc = sqrt(max(0.0f, 0.25f * diff * diff + c01 * c01));

    float lambda_major = max(0.0f, 0.5f * trace + disc);
    float lambda_minor = max(0.0f, 0.5f * trace - disc);

    float2 major_dir;

    if (abs(c01) > 0.00000001f)
    {
        major_dir = normalize(float2(lambda_major - c11, c01));
    }
    else
    {
        major_dir = (c00 >= c11)
            ? float2(1.0f, 0.0f)
            : float2(0.0f, 1.0f);
    }

    float2 minor_dir = float2(-major_dir.y, major_dir.x);

    float major_len = sqrt(lambda_major);
    float minor_len = sqrt(lambda_minor);

    // Clamp minimum visible size so tiny/degenerate splats do not vanish.
    // This is in NDC units, derived from pixels.
    float2 viewport = max(viewport_and_size.xy, float2(1.0f, 1.0f));

    float min_pixels = 1.0f;
    float min_ndc_x = (min_pixels * 2.0f) / viewport.x;
    float min_ndc_y = (min_pixels * 2.0f) / viewport.y;
    float min_ndc = max(min_ndc_x, min_ndc_y);

    major_len = max(major_len, min_ndc);
    minor_len = max(minor_len, min_ndc);

    float2 axis_major_ndc = major_dir * major_len;
    float2 axis_minor_ndc = minor_dir * minor_len;
// Draw a quad large enough to contain the visible Gaussian footprint.
// At 3 sigma, exp(-0.5 * r^2) is about 1.1% at the edge.
    float gaussian_radius = 3.0f;

    float2 ndc_offset =
    corner.x * axis_major_ndc * gaussian_radius +
    corner.y * axis_minor_ndc * gaussian_radius;

    float4 clip_pos = clip_center;
    clip_pos.xy += ndc_offset * clip_center.w;

    VSOutput output;
    output.position = clip_pos;
    output.color = input.color;
    output.opacity = input.opacity;

// uv is now in Gaussian sigma-space, not unit-disk space.
    output.uv = corner * gaussian_radius;

    return output;
}