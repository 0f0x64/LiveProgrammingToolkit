cbuffer params : register(b0)
{
    float4 position;
    float4 size;
    float4 rotation;
};

cbuffer frame : register(b2)
{
    float4 time;
    float4 aspect;
};

struct VS_OUTPUT_POS_UV
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

#define pos2 float2

VS_OUTPUT_POS_UV VS(uint vID : SV_VertexID, uint iID : SV_InstanceID)
{
    float2 map[6] = { 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 };

    float4 pos = float4(map[vID]-.5,0,1);
    pos.xy += position.xy;
    pos.y+=sin(time.x*.01)*.01;
//pos.xy +=pos2(-42.88f, -6.21f)*.01*float2(1.,-1);
    float2 uv = map[vID];
    VS_OUTPUT_POS_UV output = { pos, uv};
    return output;

}
