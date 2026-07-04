// resources/shaders/flash/flash_vs.hlsl
//
// Emissive "flash" vertex shader. Transforms a small pulled mesh (the procedural
// unit cube) by the per-node MVP. The layout's Mvp16 constants head makes the
// renderer fill c0..c3 with world * view_projection for THIS node each frame
// (rhi_scene_renderer.cpp, RenderBindingConstantsHead::Mvp16), so the VS is a
// pure transform. Positions/indices are pulled from the cube mesh
// (StructuredBuffers, no IA vertex buffer) -- the same pattern as the clipmap.

cbuffer FlashConstants : register(b0, space2)
{
    float4x4 mvp;       // c0..c3  world * view_projection (Mvp16 head)
    float    intensity; // c4.x    tail constant, driven 1->0 by cannon_flash
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);

struct VSOut { float4 pos : SV_POSITION; };

VSOut main(uint vid : SV_VertexID)
{
    float3 p = positions[indices[vid]];
    VSOut o;
    o.pos = mul(mvp, float4(p, 1.0f));
    return o;
}
