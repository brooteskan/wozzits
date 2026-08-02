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

struct PSInput
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

float3 apply_lighting(float3 color, float3 normal, float emissive)
{
    float3 n = safe_normalize_or_up(normal);
    float facing = saturate(n.y * 0.45f + 0.55f);
    return color * (0.35f + 0.65f * facing) + color * max(emissive, 0.0f);
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

float4 main(PSInput input, uint primitive_id : SV_PrimitiveID) : SV_TARGET
{
    uint rule_count = (uint)round(max(mask_params.x, 0.0f));
    uint overlap_mode = (uint)round(max(mask_params.y, 0.0f));
    uint element_count = (uint)round(max(mask_params.z, 0.0f));
    bool show_unmatched = mask_params.w >= 0.5f;
    uint mask_domain = (uint)round(max(surface_params.y, 0.0f));

    float4 selected = float4(0.0f, 0.0f, 0.0f, 0.0f);
    bool matched = false;

    if (mask_domain == 1u) {
        selected = input.mask_color;
        matched = input.mask_matched > 0.0001f;
    }
    else if (primitive_id < element_count) {
        [loop]
        for (uint i = 0u; i < rule_count && i < 16u; ++i) {
            MeshMaskRule rule = mask_rules[i];
            float value = field_values[rule.value_offset + primitive_id];
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

    float4 base = show_unmatched ? unmatched_color : surface_color;
    float4 chosen = matched ? selected : base;
    float emissive = matched ? 0.0f : surface_params.x;
    float3 lit = apply_lighting(chosen.rgb, input.normal, emissive);
    return float4(lit, saturate(chosen.a));
}
