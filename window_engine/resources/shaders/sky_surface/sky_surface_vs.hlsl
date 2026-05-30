struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut main(uint vertex_id : SV_VertexID)
{
    float2 pos;
    if (vertex_id == 0)
        pos = float2(-1.0, -1.0);
    else if (vertex_id == 1)
        pos = float2(-1.0, 3.0);
    else
        pos = float2(3.0, -1.0);

    VSOut outp;
    outp.position = float4(pos, 0.0, 1.0);
    outp.uv = pos * 0.5 + 0.5;
    return outp;
}
