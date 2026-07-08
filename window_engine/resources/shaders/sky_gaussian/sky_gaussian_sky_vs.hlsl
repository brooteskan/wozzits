// resources/shaders/sky_gaussian/sky_gaussian_sky_vs.hlsl
//
// Custom-renderable vertex shader for the spherical-Gaussian sky (Seam B1-iii,
// issue #261). The geometry is a star-shaped proxy (a unit sphere from the
// procedural sphere mesh); it carries no visual data -- its only job is to
// rasterize view DIRECTIONS.
//
// We center the proxy on the eye every frame (camera_and_diameter.xyz, filled by
// the WorldViewProjCamera36 constants head), so the origin O == the camera and
// the sky is infinitely far / parallax-free. The per-fragment quantity handed to
// the PS is the origin-relative direction  d = normalize(worldPos - O), i.e. the
// central projection of the proxy through its origin onto the unit sphere.
//
// Declared hut-style (explicit registers) so the shader is self-contained; the
// 0x104 layout binds pulled_mesh_positions -> t0, pulled_mesh_indices -> t1, and
// (in the PS) sky_gaussian -> t2, all in space2, with the 36-dword head at b0.

cbuffer SkyConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3 (declared for cbuffer offset; unused for centering)
    float4x4 view_proj;           // c4..c7
    float4   camera_and_diameter; // c8   xyz = camera world pos
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 dir : DIRECTION;   // origin-relative view direction to the PS
};

VSOut main(uint vid : SV_VertexID)
{
    float3 p  = positions[indices[vid]];        // unit-sphere local position
    float3 wp = camera_and_diameter.xyz + p;    // origin O = eye -> parallax-free

    VSOut o;
    o.dir = normalize(wp - camera_and_diameter.xyz); // == normalize(p): central projection through O
    o.pos = mul(view_proj, float4(wp, 1.0f));
    return o;
}
