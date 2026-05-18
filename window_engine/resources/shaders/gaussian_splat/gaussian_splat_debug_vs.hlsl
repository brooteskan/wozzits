cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;

    // x = viewport width
    // y = viewport height
    // z = base splat size multiplier in pixels/world-debug units
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

// Quaternion vector rotation.
// q is expected as x,y,z,w.
float3 rotate_by_quat(float3 v, float4 q)
{
    float3 qv = q.xyz;
    float qw = q.w;

    // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    float3 t = 2.0f * cross(qv, v);
    return v + qw * t + cross(qv, t);
}

float4 safe_normalize_quat(float4 q)
{
    float len_sq = dot(q, q);

    if (len_sq <= 0.0000001f)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return q * rsqrt(len_sq);
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

    // Local ellipse axes. This stage uses the splat's local X/Y scale as the
    // billboard ellipse radii. Z scale is carried in the vertex format but is
    // not yet used by this debug projection.
    float sx = max(input.scale.x, 0.000001f);
    float sy = max(input.scale.y, 0.000001f);

    // The base multiplier keeps real PLY scales visible while we are still in
    // debug-renderer territory. You can tune viewport_and_size.z from C++.
    float base_size = viewport_and_size.z;

    float3 local_axis_x = float3(sx * base_size, 0.0f, 0.0f);
    float3 local_axis_y = float3(0.0f, sy * base_size, 0.0f);

    float3 rotated_axis_x = rotate_by_quat(local_axis_x, q);
    float3 rotated_axis_y = rotate_by_quat(local_axis_y, q);

    float4 world_center = mul(world, float4(input.position, 1.0f));

    // Transform axis endpoints through world and view-projection so the ellipse
    // reacts to camera perspective and object/world transform.
    float4 world_x = mul(world, float4(input.position + rotated_axis_x, 1.0f));
    float4 world_y = mul(world, float4(input.position + rotated_axis_y, 1.0f));

    float4 clip_center = mul(view_proj, world_center);
    float4 clip_x = mul(view_proj, world_x);
    float4 clip_y = mul(view_proj, world_y);

    // Convert projected endpoints to NDC-space axes.
    float2 center_ndc = clip_center.xy / clip_center.w;
    float2 x_ndc = clip_x.xy / clip_x.w;
    float2 y_ndc = clip_y.xy / clip_y.w;

    float2 axis_x_ndc = x_ndc - center_ndc;
    float2 axis_y_ndc = y_ndc - center_ndc;

    // Fallback: if the projected axes become degenerate, keep a visible
    // screen-space billboard rather than disappearing.
    float fallback_pixels = base_size * max(sx, max(sy, input.scale.z));
    float2 viewport = viewport_and_size.xy;

    float2 fallback_axis_x = float2((fallback_pixels * 2.0f) / viewport.x, 0.0f);
    float2 fallback_axis_y = float2(0.0f, (fallback_pixels * 2.0f) / viewport.y);

    if (dot(axis_x_ndc, axis_x_ndc) < 0.0000000001f)
    {
        axis_x_ndc = fallback_axis_x;
    }

    if (dot(axis_y_ndc, axis_y_ndc) < 0.0000000001f)
    {
        axis_y_ndc = fallback_axis_y;
    }

    float2 ndc_offset =
        corner.x * axis_x_ndc +
        corner.y * axis_y_ndc;

    float4 clip_pos = clip_center;
    clip_pos.xy += ndc_offset * clip_center.w;

    VSOutput output;
    output.position = clip_pos;
    output.color = input.color;
    output.opacity = input.opacity;
    output.uv = corner;

    return output;
}