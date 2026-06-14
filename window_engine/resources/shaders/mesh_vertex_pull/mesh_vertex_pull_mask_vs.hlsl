cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4 surface_color;
    float4 mask_params;
    float4 unmatched_color;
    float4 surface_params;
};

StructuredBuffer<float3> g_positions : register(t0);
StructuredBuffer<uint>   g_indices   : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 mask_color : COLOR0;
    float mask_matched : TEXCOORD1;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    uint source_vertex = g_indices[vertex_id];
    float3 position = g_positions[source_vertex];

    VSOutput output;
    float4 world_pos = mul(world, float4(position, 1.0f));
    output.position = mul(view_proj, world_pos);
    output.normal = float3(0.0f, 1.0f, 0.0f);
    output.mask_color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    output.mask_matched = 0.0f;
    return output;
}
