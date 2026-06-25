// resources/shaders/gaussian_splat/gaussian_splat_field_cloud_vs.hlsl
//
// Scalar-field point-cloud vertex shader (issue #208, fix-2 sphere redesign).
// Renders the resident field-derived cloud as small CAMERA-FACING SPHERES, one
// per sample, so the heightmap can be read as discrete reference points
// alongside the geometry-clipmap mesh.
//
// Deliberately minimal -- the opposite of the old gaussian path. No per-splat
// covariance, eigenvalue decomposition, rotation, or scale (that math, plus the
// screen-space footprint blowing up, was the "fog"). Every sample is a uniform
// WORLD-diameter sphere placed entirely by the scene-node world transform: the
// converter authors positions in a unit [0,1] cube, and `world` (the node's
// composed transform) is the only sizing/placement. So the cloud overlays the
// clipmap exactly when both nodes carry the same transform.
//
// NO instancing and NO index buffer: the renderer issues one non-indexed
// DrawInstanced(4 * sample_count, 1). vid/4 = sample, vid&3 = quad corner.
//
// SplatView cbuffer (b0, space2) MUST match SplatCloudDrawConstants in
// rhi_scene_renderer.cpp byte-for-byte (144 bytes / 36 dwords):
//   offset  bytes  field
//      0      64   float4x4 world                (node world; column-major)
//     64      64   float4x4 view_proj
//    128      16   float4   camera_and_diameter   (xyz = camera world pos,
//                                                  w = sphere world diameter)

cbuffer SplatView : register(b0, space2)
{
    float4x4 world;
    float4x4 view_proj;
    float4   camera_and_diameter;  // xyz = camera world pos, w = sphere diameter
};

// Resident decoded sample (64 bytes, offsets pinned by a static_assert on the
// CPU side). The sphere path only reads position / color; scale + rotation are
// carried for format compatibility but unused (spheres are uniform).
struct Splat
{
    float3 position;   // offset  0  unit-cube [0,1] sample position
    float  opacity;    // offset 12  (unused by the opaque sphere path)
    float3 scale;      // offset 16  (unused)
    float  pad0;       // offset 28
    float4 rotation;   // offset 32  (unused)
    float3 color;      // offset 48  decoded linear RGB
    uint   pad1;       // offset 60
};

StructuredBuffer<Splat> g_splats : register(t0, space2);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;  // [-1,1] across the quad, for the sphere mask
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    // 6 verts per sphere = two self-contained CW triangles (the program is a
    // TriangleList, so a 4-vert quad would let triangles span ADJACENT splats and
    // shatter into stretched/partial shapes). vid/6 selects the sample; vid%6
    // indexes the two-triangle unit quad below.
    uint  sample_index = vertex_id / 6u;
    Splat s            = g_splats[sample_index];

    float2 quad[6] =
    {
        float2(-1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f, -1.0f),  // tri 1
        float2( 1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f,  1.0f)   // tri 2
    };
    float2 corner = quad[vertex_id % 6u];

    // World-space sample center -- the node transform is the ONLY sizing.
    float3 center = mul(world, float4(s.position, 1.0f)).xyz;

    // Camera-facing billboard basis. radius is a uniform WORLD size, so the
    // spheres sit in the scene and shrink with distance (not a screen overlay).
    float3 cam_pos = camera_and_diameter.xyz;
    float  radius  = max(camera_and_diameter.w, 0.0f) * 0.5f;

    float3 view_dir = center - cam_pos;
    float  dist     = length(view_dir);
    float3 fwd      = dist > 1e-5f ? view_dir / dist : float3(0.0f, 0.0f, 1.0f);
    // Guard the degenerate up_hint when looking near-vertically at a sample.
    float3 up_hint  = abs(fwd.y) > 0.99f ? float3(0.0f, 0.0f, 1.0f)
                                         : float3(0.0f, 1.0f, 0.0f);
    float3 right    = normalize(cross(up_hint, fwd));
    float3 up       = cross(fwd, right);

    float3 corner_ws = center
                     + corner.x * radius * right
                     + corner.y * radius * up;

    VSOutput o;
    o.position = mul(view_proj, float4(corner_ws, 1.0f));
    o.color    = s.color;
    o.uv       = corner;
    return o;
}
