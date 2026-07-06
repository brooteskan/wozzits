// resources/shaders/hut/hut_ps.hlsl
//
// Custom-renderable pixel shader for the "hut". Flat-shaded directional
// lighting: a per-face normal is reconstructed from screen-space derivatives of
// the interpolated world position, so every face of the cube reads as a
// distinct planar surface without needing real vertex normals (the procedural
// cube ships placeholder (0,0,1) normals -- see procedural_mesh.cpp).
//
// The cbuffer MUST be declared identically to hut_vs.hlsl so the authored
// 'base_color' tail constant resolves to c9.xyz (the 36-dword WorldViewProjCamera36
// head occupies c0..c8). We read base_color + the camera position; world/view_proj
// are declared only to pin the offset.

cbuffer HutConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3  (declared for offset only)
    float4x4 view_proj;           // c4..c7  (declared for offset only)
    float4   camera_and_diameter; // c8   xyz = camera world pos
    float3   base_color;          // c9.xyz  authored tint
};

struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;
};

float4 main(PSInput input) : SV_TARGET
{
    // Flat face normal from the world-position gradient across the triangle.
    float3 n = normalize(cross(ddx(input.world_pos), ddy(input.world_pos)));

    // Two-side the normal toward the viewer so lighting is correct regardless of
    // triangle winding / cull state (the visible hemisphere always lights).
    float3 view_dir = normalize(camera_and_diameter.xyz - input.world_pos);
    if (dot(n, view_dir) < 0.0f) {
        n = -n;
    }

    // Fixed key light from up-and-to-the-side (world space), plus flat ambient so
    // faces turned away from the light don't go fully black.
    const float3 light_dir = normalize(float3(0.4f, 0.85f, 0.35f));
    const float  ambient   = 0.25f;

    float  ndotl = saturate(dot(n, light_dir));
    float3 lit   = base_color * (ambient + (1.0f - ambient) * ndotl);

    return float4(lit, 1.0f);
}
