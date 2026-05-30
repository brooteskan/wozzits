cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4 light_position_range;
    float4 light_direction_type;
    float4 light_color_intensity;
    float4 ambient_color_type;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 world_pos : WORLDPOS;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float light_type = ambient_color_type.w;
    bool hdri_lighting = light_type < -0.5f;
    float3 light_vec = normalize(-light_direction_type.xyz);
    float attenuation = 1.0f;

    if (!hdri_lighting && light_type > 0.5f)
    {
        float3 to_light = light_position_range.xyz - input.world_pos;
        float distance_to_light = length(to_light);
        light_vec = distance_to_light > 1e-4f
            ? to_light / distance_to_light
            : float3(0.0f, 1.0f, 0.0f);
        float range = max(light_position_range.w, 1e-4f);
        attenuation = saturate(1.0f - distance_to_light / range);
        attenuation *= attenuation;
    }

    float n_dot_l = saturate(dot(n, light_vec));
    float3 direct =
        light_color_intensity.rgb
        * light_color_intensity.w
        * n_dot_l
        * attenuation;

    float3 environment = ambient_color_type.rgb;
    if (hdri_lighting)
    {
        // HDRI terrain lighting starts with resolved metadata. Future generated
        // irradiance/prefiltered maps can replace this approximation while
        // keeping terrain independent from scene ambient/directional lights.
        float sky_visibility_strength = saturate(light_position_range.x);
        float terrain_bounce_strength = max(light_position_range.y, 0.0f);
        float sky_visibility = lerp(
            1.0f,
            saturate(n.y * 0.5f + 0.5f),
            sky_visibility_strength);
        float ground_bounce = terrain_bounce_strength * saturate(-n.y);
        environment *= sky_visibility + ground_bounce;
    }

    float3 low = float3(0.20f, 0.34f, 0.18f);
    float3 high = float3(0.54f, 0.50f, 0.36f);
    float3 base = lerp(low, high, saturate(input.uv.y));

    return float4(base * (environment + direct), 1.0f);
}
