cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4 surface_color;
    float4 mask_params;
    float4 unmatched_color;
    float4 surface_params;
};

StructuredBuffer<float> field_values : register(t0);

struct MeshMaskRule
{
    float4 color;
    float lo;
    float hi;
    uint value_offset;
    int priority;
};

StructuredBuffer<MeshMaskRule> mask_rules : register(t1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 mask_color : COLOR0;
    float mask_matched : TEXCOORD1;
};

float3 safe_normalize_or_up(float3 value)
{
    float len_sq = dot(value, value);
    // Reject direction -- see issue #316: NaN skips an accept-direction gate.
    if (!isfinite(len_sq) || !(len_sq > 1.0e-8f)) {
        return float3(0.0f, 1.0f, 0.0f);
    }
    return value * rsqrt(len_sq);
}

float4 alpha_over(float4 dst, float4 src)
{
    float src_a = saturate(src.a);
    float dst_a = saturate(dst.a);
    float out_a = src_a + dst_a * (1.0f - src_a);
    if (!(out_a > 0.0001f)) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 out_rgb =
        (src.rgb * src_a + dst.rgb * dst_a * (1.0f - src_a)) / out_a;
    return float4(out_rgb, out_a);
}

float4 evaluate_vertex_mask(uint vertex_id, out bool matched)
{
    uint rule_count = (uint)round(max(mask_params.x, 0.0f));
    uint overlap_mode = (uint)round(max(mask_params.y, 0.0f));
    uint element_count = (uint)round(max(mask_params.z, 0.0f));

    float4 selected = float4(0.0f, 0.0f, 0.0f, 0.0f);
    matched = false;

    if (vertex_id < element_count) {
        [loop]
        for (uint i = 0u; i < rule_count && i < 16u; ++i) {
            MeshMaskRule rule = mask_rules[i];
            float value = field_values[rule.value_offset + vertex_id];
            if (value >= rule.lo && value <= rule.hi) {
                matched = true;
                if (overlap_mode == 1u) {
                    selected = alpha_over(selected, rule.color);
                }
                else {
                    selected = rule.color;
                }
            }
        }
    }

    return selected;
}

VSOutput main(VSInput input, uint vertex_id : SV_VertexID)
{
    VSOutput output;
    float4 world_pos = mul(world, float4(input.position, 1.0f));
    output.position = mul(view_proj, world_pos);
    output.normal = safe_normalize_or_up(mul((float3x3)world, input.normal));
    output.mask_color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    output.mask_matched = 0.0f;

    uint mask_domain = (uint)round(max(surface_params.y, 0.0f));
    if (mask_domain == 1u) {
        bool matched = false;
        output.mask_color = evaluate_vertex_mask(vertex_id, matched);
        output.mask_matched = matched ? 1.0f : 0.0f;
    }
    return output;
}
