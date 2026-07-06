// resources/shaders/hut/hut_vs.hlsl
//
// Custom-renderable vertex shader for the "hut" (the procedural unit cube,
// asset-graph node hut_cube). Transforms the pulled cube into clip space AND
// carries world-space position to the PS so the pixel shader can reconstruct a
// flat per-face normal for directional lighting.
//
// Constants head = WorldViewProjCamera36 (render_binding_constants.h, ordinal 2).
// The renderer fills the first 36 dwords of the cbuffer each frame from THIS
// node's transform + the live camera (rhi_scene_renderer.cpp,
// make_splat_cloud_draw_constants / SplatCloudDrawConstants, 144 bytes):
//
//   offset  reg    field
//      0    c0..c3 float4x4 world              (this node's world transform)
//     64    c4..c7 float4x4 view_proj          (view * projection)
//    128    c8     float4   camera_and_diameter (xyz = camera world pos; w = 0
//                                                for a custom renderable)
//    144    c9.xyz float3   base_color          (authored tail constant; the
//                                                layout declares it right after
//                                                the 36-dword head, so it packs
//                                                to c9. Behavior-drivable via
//                                                SET_RENDERABLE_PARAM.)
//
// Declare the full head in BOTH stages so 'base_color' lands at c9 in each
// (same discipline as flash_vs.hlsl declaring 'intensity' at c4.x). The cube's
// stored normals are placeholder (0,0,1), so we deliberately do NOT pull them --
// the PS derives the face normal from ddx/ddy(world_pos) instead.

cbuffer HutConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3
    float4x4 view_proj;           // c4..c7
    float4   camera_and_diameter; // c8   (xyz = camera world pos)
    float3   base_color;          // c9.xyz  authored tail (unused in the VS)
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);

struct VSOut
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;   // for the PS's derivative-based face normal
};

VSOut main(uint vid : SV_VertexID)
{
    float3 p  = positions[indices[vid]];
    float3 wp = mul(world, float4(p, 1.0f)).xyz;

    VSOut o;
    o.world_pos = wp;
    o.pos       = mul(view_proj, float4(wp, 1.0f));
    return o;
}
