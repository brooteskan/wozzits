// resources/shaders/haze/haze_vs.hlsl
//
// Vertex shader for the HAZE DOME -- a camera-following atmospheric-haze
// renderable that veils distant geometry into the authored fog colour, so the
// far terrain fades toward the horizon. A new custom renderable, NOT a change to
// any terrain shader: fog is composed as its own scene node.
//
// Same eye-centering trick as the SG sky proxy (sky_gaussian_sky_vs): a unit
// sphere from the procedural sphere mesh is translated to the camera every frame,
// so the dome moves with the viewer. Two differences from the sky:
//
//   1. It is SIZED. The sky is parallax-free (radius irrelevant); the haze sits
//      at a finite radius so nearer terrain, which wrote closer depth, OCCLUDES
//      it (the program runs DepthMode::TestNoWrite). The radius is the scene
//      node's scale -- transform is the sole sizing knob (no world-size on the
//      renderable), so `world` (the WorldViewProjCamera36 head's node matrix,
//      which the sky ignores) rotates/scales the unit sphere here.
//
//   2. It hands the PS the dome DIRECTION, whose elevation shapes the haze band
//      (dense at the horizon, thinning upward) in haze_ps.
//
// Declared hut-style (explicit registers) so the shader is self-contained; the
// 0x104 layout binds the 36-dword WorldViewProjCamera36 head at b0/space2 and
// pulled_mesh_positions/indices at t0/t1 space2.

cbuffer HazeConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3  node transform (scale sizes the dome)
    float4x4 view_proj;           // c4..c7
    float4   camera_and_diameter; // c8      xyz = camera world pos
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 dir : DIRECTION;   // dome direction; its elevation shapes the band
};

VSOut main(uint vid : SV_VertexID)
{
    float3 p  = positions[indices[vid]];          // unit-sphere local position

    // Node scale/rotation sizes the dome (translation dropped -- we recenter on
    // the eye). float3x3 cast takes the upper-left, discarding translation.
    float3 sp = mul((float3x3)world, p);
    float3 wp = camera_and_diameter.xyz + sp;     // camera-following

    VSOut o;
    // Direction on the UNIT sphere, independent of the node scale, so the band
    // shape does not change as the dome is resized.
    o.dir = normalize(p);
    o.pos = mul(view_proj, float4(wp, 1.0f));
    return o;
}
