// resources/shaders/puppet/puppet_vs.hlsl
//
// An Inochi2D puppet Part's screen-space vertex shader. A Part is a 2D triangle
// mesh authored in the puppet's local pixel space; the object block carries the
// Part's precomputed 2D affine placement (puppet-local pixels -> screen pixels)
// plus its opacity for the pixel shader. Vertices (interleaved pos+uv) and
// indices are PULLED from StructuredBuffers -- the same vertex-pull model the
// overlay renderable uses (seam 4) -- so there is no input-assembler layout.
//
// The Screen view head (seam 2) supplies the viewport at t0 of register space 0,
// filled once per frame by the renderer, so the puppet keeps its pixel size at
// any resolution. Parts are recorded in the Overlay draw layer (seam 3),
// depth-disabled, in zsort order -- static render is the inochi runtime's S2.
//
// The object SRG layout mirrors the overlay's: two pull SRVs (t0 vertices, t1
// indices) then the atlas at t2, a static sampler at s0, and the per-Part
// constant block at b0 -- all in space 2.

struct WzScreenConstants
{
    float4 viewport;   // xy = width, height; zw = 1/width, 1/height
};
StructuredBuffer<WzScreenConstants> screen_constants : register(t0, space0);

cbuffer PuppetPartBlock : register(b0, space2)
{
    // A 2D affine (puppet-local pixels -> screen pixels), packed row-major:
    //   screen_px.x = row0.x * v.x + row0.y * v.y + row0.z
    //   screen_px.y = row1.x * v.x + row1.y * v.y + row1.z
    float4 xform_row0;  // a, b, tx, opacity
    float4 xform_row1;  // c, d, ty, (unused)
    // Per-Part colour modulation, consumed by the PS only (#276). Declared here
    // too so the VS and PS agree on the block the root signature provides.
    float4 part_tint;        // rgb = multiply tint, w unused
    float4 part_screen_tint; // rgb = screen tint,   w unused
    float4 part_mask;        // x = mask threshold, y = invert, zw unused
};

struct WzPuppetVertex
{
    float2 pos;   // puppet-local pixel space
    float2 uv;    // atlas-relative [0,1]
};
StructuredBuffer<WzPuppetVertex> vertices : register(t0, space2);
StructuredBuffer<uint>           indices  : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    // Target-space [0,1] position, for sampling the Part's coverage mask (#275).
    // The mask texture is rendered at the TARGET's dimensions, so this is just
    // the screen-pixel position normalised -- computed here because the pixel
    // shader has no view of the viewport (the Screen head is Vertex-visible).
    float2 mask_uv : TEXCOORD1;
};

VSOut main(uint vid : SV_VertexID)
{
    uint           idx = indices[vid];
    WzPuppetVertex v   = vertices[idx];

    // Part-local pixel -> screen pixel via the affine placement.
    float2 px = float2(
        xform_row0.x * v.pos.x + xform_row0.y * v.pos.y + xform_row0.z,
        xform_row1.x * v.pos.x + xform_row1.y * v.pos.y + xform_row1.z);

    // Pixels are y-down; NDC is y-up. Map to NDC with the viewport, then flip Y.
    float2 vp  = screen_constants[0].viewport.xy;
    float2 ndc = px * (2.0f / vp) - 1.0f;

    VSOut o;
    o.pos     = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    o.uv      = v.uv;
    o.mask_uv = px / vp;
    return o;
}
