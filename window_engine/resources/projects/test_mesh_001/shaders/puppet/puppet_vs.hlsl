
struct WzScreenConstants
{
    float4 viewport;
};
StructuredBuffer<WzScreenConstants> screen_constants : register(t0, space0);

cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;
    float4 xform_row1;
    float4 part_tint;
    float4 part_screen_tint;
    float4 part_mask;
    uint4  part_pull;
};

struct WzPuppetVertex
{
    float2 pos;
    float2 uv;
};
StructuredBuffer<WzPuppetVertex> vertices : register(t0, space2);
StructuredBuffer<uint>           indices  : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float2 mask_uv : TEXCOORD1;
};

VSOut main(uint vid : SV_VertexID)
{
    uint           idx = indices[part_pull.y + vid];
    WzPuppetVertex v   = vertices[part_pull.x + idx];
    float2 px = float2(
        xform_row0.x * v.pos.x + xform_row0.y * v.pos.y + xform_row0.z,
        xform_row1.x * v.pos.x + xform_row1.y * v.pos.y + xform_row1.z);
    float2 vp  = screen_constants[0].viewport.xy;
    float2 ndc = px * (2.0f / vp) - 1.0f;
    VSOut o;
    o.pos     = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    o.uv      = v.uv;
    o.mask_uv = px / vp;
    return o;
}
