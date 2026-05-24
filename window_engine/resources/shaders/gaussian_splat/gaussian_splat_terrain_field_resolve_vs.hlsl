// shaders/gaussian_splat/gaussian_splat_terrain_field_resolve_vs.hlsl
//
// Fullscreen triangle vertex shader for the terrain field-blend resolve pass.
// Generates a single triangle that covers the entire viewport from
// SV_VertexID (0, 1, 2).  No vertex buffer required.

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    // Fullscreen triangle: vertex 0 = (-1,-1), 1 = (-1,3), 2 = (3,-1)
    // UV maps [0,1] across the viewport.
    float2 uv = float2(
        (vertex_id << 1) & 2,   // 0, 2, 0
         vertex_id       & 2);  // 0, 0, 2

    VSOutput output;
    output.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    // Flip Y for DX texture coords (top-left origin).
    output.uv = float2(uv.x, 1.0f - uv.y);
    return output;
}
