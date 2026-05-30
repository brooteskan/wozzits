cbuffer SkySurfaceConstants : register(b0)
{
    float4 sky_color_exposure;
    float4 sky_gradient_top;
    float4 sky_gradient_bottom;
    float4 sky_mode_rotation0;
    float4 sky_rotation1_right;
    float4 sky_up;
    float4 sky_forward;
};

Texture2D<float4> sky_field_texture : register(t0);
SamplerState sky_sampler : register(s0);

float3 rotate_x(float3 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float3(v.x, v.y * c - v.z * s, v.y * s + v.z * c);
}

float3 rotate_y(float3 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}

float3 rotate_z(float3 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float3 sky_color = sky_color_exposure.rgb;
    float exposure = sky_color_exposure.w;
    float3 gradient_top_color = sky_gradient_top.rgb;
    float3 gradient_bottom_color = sky_gradient_bottom.rgb;
    float visual_kind = sky_mode_rotation0.x;
    float rotation_x = sky_mode_rotation0.z;
    float rotation_y = sky_mode_rotation0.w;
    float rotation_z = sky_rotation1_right.x;
    float3 camera_right = sky_rotation1_right.yzw;
    float3 camera_up = sky_up.xyz;
    float3 camera_forward = sky_forward.xyz;

    float2 p = uv * 2.0 - 1.0;
    float3 view_dir = normalize(float3(p.x, p.y, 1.0));
    float3 dir = normalize(
        camera_right * view_dir.x
        + camera_up * view_dir.y
        + camera_forward * view_dir.z);
    dir = rotate_z(rotate_y(rotate_x(dir, rotation_x), rotation_y), rotation_z);

    if (visual_kind > 1.5 && visual_kind < 2.5)
    {
        // DirectionDebug is the first projection test in the sky-surface ladder.
        // It intentionally needs no external resources.
        float longitude_band = step(0.96, frac((atan2(dir.x, dir.z) / 6.2831853 + 0.5) * 12.0));
        float latitude_band = step(0.96, frac((asin(dir.y) / 3.14159265 + 0.5) * 8.0));
        float3 base_color = dir * 0.5 + 0.5;
        float grid = max(longitude_band, latitude_band);
        return float4(lerp(base_color, float3(1.0, 1.0, 1.0), grid), 1.0);
    }

    if (visual_kind > 2.5 && visual_kind < 3.5)
    {
        float t = saturate(dir.y * 0.5 + 0.5);
        float3 color = lerp(gradient_bottom_color, gradient_top_color, t);
        return float4(saturate(color * max(exposure, 0.0)), 1.0);
    }

    if (visual_kind > 3.5 && visual_kind < 4.5)
    {
        float u = atan2(dir.x, dir.z) / 6.2831853 + 0.5;
        float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
        float3 color = sky_field_texture.Sample(sky_sampler, float2(u, v)).rgb;
        return float4(saturate(color * max(exposure, 0.0)), 1.0);
    }

    if (visual_kind > 4.5 && visual_kind < 5.5)
    {
        float u = atan2(dir.x, dir.z) / 6.2831853 + 0.5;
        float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
        float sample_value = saturate(sky_field_texture.Sample(sky_sampler, float2(u, v)).r);
        float3 color = lerp(gradient_bottom_color, gradient_top_color, sample_value);
        return float4(saturate(color * max(exposure, 0.0)), 1.0);
    }

    if (visual_kind > 5.5 && visual_kind < 6.5)
    {
        float u = atan2(dir.x, dir.z) / 6.2831853 + 0.5;
        float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
        float3 vector_value = sky_field_texture.Sample(sky_sampler, float2(u, v)).xyz;
        float len = length(vector_value);
        float3 direction_color = vector_value * 0.5 + 0.5;
        float3 color = lerp(gradient_bottom_color, direction_color, saturate(len));
        return float4(saturate(color * max(exposure, 0.0)), 1.0);
    }

    return float4(saturate(sky_color * max(exposure, 0.0)), 1.0);
}
