// resources/shaders/clipmap/clipmap_wire_ps.hlsl
//
// Solid-fill wireframe pixel shader for the clipmap landscape.
//
// The FILL_WIREFRAME raster path (RasterMode::WireframeCullNone, the clipmap's
// original program) only rasterises triangle EDGES, so interiors never write
// depth and far hills show through near ones ("see-through"). This PS instead
// runs on a SOLID raster program (RasterMode::SolidCullNone, DepthMode::
// TestWrite): every interior pixel is shaded AND writes depth, so nearer terrain
// correctly OCCLUDES farther terrain. It still LOOKS like a wireframe because the
// interior is painted the renderer clear colour -- so the fill reads as the dark
// background and disappears into it -- and only pixels near a triangle edge get
// the line colour.
//
// The wireframe LINES reuse the exact height-band tint + lambertian shading of
// the shaded clipmap_ps (elevation green->brown->white, lit by a fixed key
// light), so the wireframe keeps the colours of the original see-through version
// -- just drawn on a solid, depth-writing surface so near hills hide far ones.
//
// Edge detection is barycentric: clipmap_wire_vs (the shared clipmap VS) emits a
// per-corner basis (1,0,0)/(0,1,0)/(0,0,1) that the rasteriser interpolates, so
// each pixel's bary component is its distance to the opposite edge. fwidth()
// turns that into a screen-space-constant line width.
//
// Reads b0 for vertical_scale/base_height (to normalise world height into the
// tint band); the layout marks the constants All-visible, so the PS sees them.

cbuffer Clipmap : register(b0, space2)
{
    float4x4 view_projection;
    float4   snap_params;
    float4   world_to_uv;
    float4   texel_and_vertical;  // xy = texel world size, z = vscale, w = base
    float4   texel_dims_extent;
};

struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : TEXCOORD0;
    float3 normal    : NORMAL;
    float3 bary      : TEXCOORD1;
};

// Wireframe line width in pixels (half-width of the smoothstep band).
#define CLIPMAP_WIRE_WIDTH 1.0f

float4 main(PSInput input) : SV_TARGET
{
    // --- Barycentric edge factor: 1 on a triangle edge, 0 in the interior. Each
    //     bary component is 0 on one edge and 1 at the opposite vertex, so the
    //     smallest is the distance to the nearest edge; fwidth() rescales it to a
    //     constant pixel width regardless of foreshortening. ---
    float3 d    = fwidth(input.bary);
    float3 ramp = smoothstep(float3(0.0f, 0.0f, 0.0f),
                             d * CLIPMAP_WIRE_WIDTH,
                             input.bary);
    float  edge = 1.0f - min(min(ramp.x, ramp.y), ramp.z);

    // --- Line colour: the shaded clipmap_ps height-band tint (the colours from
    //     the original non-occluding wireframe). Lambertian shade from a fixed
    //     key light, times an elevation ramp green -> brown -> white. ---
    float3 n         = normalize(input.normal);
    float3 light_dir = normalize(float3(0.35f, 0.85f, 0.30f));
    float  ndotl     = saturate(dot(n, light_dir));
    float  shade     = 0.30f + 0.70f * ndotl;

    float vertical_scale = texel_and_vertical.z;
    float base_height    = texel_and_vertical.w;
    float h01 = saturate(
        (input.world_pos.y - base_height) / max(vertical_scale, 1e-3f));

    float3 low  = float3(0.20f, 0.42f, 0.18f);   // vegetated low ground
    float3 mid  = float3(0.52f, 0.42f, 0.28f);   // rocky slope
    float3 high = float3(0.92f, 0.92f, 0.95f);   // snow cap
    float3 albedo =
        (h01 < 0.5f)
            ? lerp(low, mid, h01 * 2.0f)
            : lerp(mid, high, (h01 - 0.5f) * 2.0f);
    float3 line_col = albedo * shade;

    // --- Interior: the dark renderer clear colour (editor_runtime.cpp clears to
    //     0.10,0.10,0.12), so the filled terrain occludes without being seen. ---
    float3 interior = float3(0.10f, 0.10f, 0.12f);

    return float4(lerp(interior, line_col, edge), 1.0f);
}
