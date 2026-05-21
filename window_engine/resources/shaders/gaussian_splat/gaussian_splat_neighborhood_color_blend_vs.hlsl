// shaders/gaussian_splat/gaussian_splat_neighborhood_color_blend_vs.hlsl
//
// Splat color LOD shader.  Same geometry math as the pull-debug VS, but
// the colour output is selected/blended from one of four modes driven by
// scene-wide root constants:
//
//   mode 0 — Natural          (base color only; equivalent to pull-debug)
//   mode 1 — LODOnly          (neighborhood color only; debug)
//   mode 2 — Confidence       (confidence as grayscale; debug)
//   mode 3 — Blended          (smoothstep distance/stride blend)
//
// Modes 0–2 are intentionally "hard" debug paths to verify packing and
// upload before trusting the smoothstep blend (mode 3).
//
// Root signature (must match render_program_compilers for
// GaussianSplatNeighborhoodColorBlend):
//   param 0  b0  48 x float root constants (Vertex-only)
//                  [0..15]   world matrix
//                  [16..31]  view_proj matrix
//                  [32..35]  viewport_and_size: vp_w, vp_h, debug_scale, _
//                  [36..39]  lod_params0:  mode, strength, near, far
//                  [40..43]  lod_params1:  stride_ratio, max_stride,
//                                          use_confidence, _
//                  [44..47]  reserved/pad
//   param 1  t0  StructuredBuffer<Splat>   (Vertex-only)
//   param 2  t1  StructuredBuffer<uint>    (Vertex-only)  — sorted indices

cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;

    // x = viewport width
    // y = viewport height
    // z = debug scale multiplier
    // w = unused
    float4 viewport_and_size;

    // mode, strength, near_distance, far_distance
    float4 lod_params0;

    // stride_ratio (rendered/total), max_stride_for_blend, use_confidence, _
    float4 lod_params1;

    float4 lod_pad;
};

// Mirror of DX12GaussianSplatVertex (64 bytes; offsets pinned by CPU-side
// static_asserts).  Field order is load-bearing.
struct Splat
{
    float3 position;                    // offset  0
    float  opacity;                     // offset 12
    float3 scale;                       // offset 16
    float  pad0;                        // offset 28
    float4 rotation;                    // offset 32  x,y,z,w (normalised)
    float3 color;                       // offset 48
    uint   lod_color_confidence_rgba8;  // offset 60  packed: R|G<<8|B<<16|A<<24
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

// Unpack packed RGBA8 LOD slot into linear RGB + confidence in [0,1].
void unpack_lod_color_confidence(
    uint   packed,
    out float3 lod_color,
    out float  confidence)
{
    const float inv255 = 1.0 / 255.0;
    lod_color.r = float( packed        & 0xFFu) * inv255;
    lod_color.g = float((packed >>  8) & 0xFFu) * inv255;
    lod_color.b = float((packed >> 16) & 0xFFu) * inv255;
    confidence  = float((packed >> 24) & 0xFFu) * inv255;
}

VSOutput main(uint vertex_id   : SV_VertexID,
              uint instance_id : SV_InstanceID)
{
    // ── Geometry (identical to pull-debug) ─────────────────────────────
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

    // ── Color selection / blend ────────────────────────────────────────
    float3 base_color = s.color;

    float3 lod_color;
    float  lod_confidence;
    unpack_lod_color_confidence(
        s.lod_color_confidence_rgba8, lod_color, lod_confidence);

    const uint  mode           = (uint)lod_params0.x;
    const float strength       = lod_params0.y;
    const float near_distance  = lod_params0.z;
    const float far_distance   = lod_params0.w;

    const float stride_ratio   = lod_params1.x;
    const float max_stride     = lod_params1.y;
    const float use_confidence = lod_params1.z;

    float3 out_color = base_color;

    if (mode == 1u)
    {
        // LODOnly — show the packed neighborhood color directly.
        out_color = lod_color;
    }
    else if (mode == 2u)
    {
        // Confidence as grayscale.
        out_color = float3(lod_confidence, lod_confidence, lod_confidence);
    }
    else if (mode == 3u)
    {
        // Blended.  blend_amount drives lerp(base, lod, t).
        //   distance factor: smoothstep over view-space Z
        //   stride factor:   smoothstep over effective stride
        //   confidence:      optional multiplier
        //   strength:        global scale

        // View-space depth is clip_center.w with row-major mul convention
        // used by safe identity view; clip_center already includes view_proj
        // so its .w is the linear camera-space depth.
        float view_z = max(clip_center.w, 0.0001f);

        float distance_factor = smoothstep(
            near_distance, max(far_distance, near_distance + 1e-3f), view_z);

        float stride_factor = 0.0f;
        if (max_stride > 1.001f && stride_ratio > 0.0f)
        {
            float effective_stride = 1.0f / stride_ratio;
            stride_factor = smoothstep(1.0f, max_stride, effective_stride);
        }

        float blend = max(distance_factor, stride_factor);

        if (use_confidence > 0.5f)
            blend *= lod_confidence;

        blend = saturate(blend * strength);

        out_color = lerp(base_color, lod_color, blend);
    }
    // mode == 0 (Natural) falls through: out_color = base_color.

    VSOutput output;
    output.position = clip_pos;
    output.color    = out_color;
    output.opacity  = s.opacity;
    output.uv       = corner * gaussian_radius;
    return output;
}
