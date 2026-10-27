#define Texture2D(name)       struct name##_dummy {}
#define SamplerState(name)    struct name##_dummy {}

// Маскируем семантику регистров дл текстур/сэмплеров
#define register(x)

// Превращаем cbuffer в обычную C++ структуру с выравниванием по 16 байт (требование DirectX)
#define cbuffer               struct alignas(16)

// Алиасы для HLSL типов данных в C++ (используя DirectXMath)
#define float2                DirectX::XMFLOAT2
#define float3                DirectX::XMFLOAT3
#define float4                DirectX::XMFLOAT4
#define matrix                DirectX::XMMATRIX
#define float4x4              DirectX::XMMATRIX
#define uint                  unsigned int

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
    return float4(1-3.95*length(input.uv-.5),0,0,3);

}