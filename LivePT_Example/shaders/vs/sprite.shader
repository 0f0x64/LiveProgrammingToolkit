struct VS_OUTPUT_POS_UV
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VS_OUTPUT_POS_UV VS(uint vID : SV_VertexID, uint iID : SV_InstanceID)
{
    float2 map[6] = { 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 };
    float4 pos = float4(map[vID]-.5,0,1);
    float2 uv = map[vID];
    VS_OUTPUT_POS_UV output = { pos, uv};
    return output;

}