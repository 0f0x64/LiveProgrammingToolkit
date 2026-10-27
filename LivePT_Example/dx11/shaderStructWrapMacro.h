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