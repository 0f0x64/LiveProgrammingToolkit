Texture2D screen : register(t0);
SamplerState sam1 : register(s0);

cbuffer params : register(b0)
{
    float hilight;
};

struct VS_OUTPUT_POS_UV
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 PS(VS_OUTPUT_POS_UV input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    //float4 c = screen.SampleLevel(sam1,input.uv,0);
    //return float4(c.rgb,1);
    return float4(1,0,0,1);

}