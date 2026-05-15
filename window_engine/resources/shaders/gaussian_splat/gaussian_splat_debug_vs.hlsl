cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;

    // x = viewport width
    // y = viewport height
    // z = base splat size in pixels
    // w = unused
    float4 viewport_and_size;
};

struct VSInput
{
    float3 position : POSITION;
    float scale : SCALE;
    float3 color : COLOR;
    float opacity : OPACITY;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float opacity : OPACITY;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input, uint vertex_id : SV_VertexID)
{
    float2 corners[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 1.0f),
        float2(1.0f, -1.0f),
        float2(1.0f, 1.0f)
    };

    float2 corner = corners[vertex_id & 3];

    float4 world_pos = mul(world, float4(input.position, 1.0f));
    float4 clip_pos = mul(view_proj, world_pos);

    float2 viewport = viewport_and_size.xy;
    float splat_pixels = viewport_and_size.z * input.scale;

    float2 ndc_offset = corner * float2(
        (splat_pixels * 2.0f) / viewport.x,
        (splat_pixels * 2.0f) / viewport.y
    );

    clip_pos.xy += ndc_offset * clip_pos.w;

    VSOutput output;
    output.position = clip_pos;
    output.color = input.color;
    output.opacity = input.opacity;
    output.uv = corner;

    return output;
}