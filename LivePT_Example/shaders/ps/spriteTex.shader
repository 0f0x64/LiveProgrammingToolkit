Texture2D screen : register(t0);
SamplerState sam1 : register(s0);

cbuffer params : register(b0)
{
    float4 base_color;
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

float3 hashGradient(float n)
{
    float x = frac(sin(n) * 43758.5453123);
    float y = frac(sin(n + 12.34) * 43758.5453123);
    float z = frac(sin(n + 56.78) * 43758.5453123);
    return -1.0 + 2.0 * float3(x, y, z);
}

float perlinNoise3D(float3 x)
{
    float3 p = floor(x);
    float3 f = frac(x);
 
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
 
    float n = p.x + p.y * 57.0 + 113.0 * p.z;
 
    float3 g000 = f - float3(0.0, 0.0, 0.0);
    float3 g100 = f - float3(1.0, 0.0, 0.0);
    float3 g010 = f - float3(0.0, 1.0, 0.0);
    float3 g110 = f - float3(1.0, 1.0, 0.0);
    float3 g001 = f - float3(0.0, 0.0, 1.0);
    float3 g101 = f - float3(1.0, 0.0, 1.0);
    float3 g011 = f - float3(0.0, 1.0, 1.0);
    float3 g111 = f - float3(1.0, 1.0, 1.0);
 
    float d000 = dot(hashGradient(n + 0.0),   g000);
    float d100 = dot(hashGradient(n + 1.0),   g100);
    float d010 = dot(hashGradient(n + 57.0),  g010);
    float d110 = dot(hashGradient(n + 58.0),  g110);
    float d001 = dot(hashGradient(n + 113.0), g001);
    float d101 = dot(hashGradient(n + 114.0), g101);
    float d011 = dot(hashGradient(n + 170.0), g011);
    float d111 = dot(hashGradient(n + 171.0), g111);
 
    float a = lerp(lerp(lerp(d000, d100, u.x),
                        lerp(d010, d110, u.x), u.y),
                   lerp(lerp(d001, d101, u.x),
                        lerp(d011, d111, u.x), u.y), u.z);
    
    return a;
}

float3 Interpolate4ColorsSmooth(float t, float4 colors[4])
{
    float f0 = smoothstep(colors[0].a, colors[1].a, t);
    float f1 = smoothstep(colors[1].a, colors[2].a, t);
    float f2 = smoothstep(colors[2].a, colors[3].a, t);

    float3 c01    = lerp(colors[0].rgb, colors[1].rgb, f0);
    float3 c012   = lerp(c01,           colors[2].rgb, f1);
    float3 result = lerp(c012,          colors[3].rgb, f2);

    return result;
}

float mask(float2 uv)
{
    float a = 0;
    for (int i=0;i<20;i++)
    {
        float r=0;
    float t= screen.SampleLevel(sam1,uv+float2(r,-i*.005),2).a;
    float t2= screen.SampleLevel(sam1,uv+float2(r,-i*.005+0.02),2).a;
    a+=abs(t-t2)*3*(20-i);
    }
    return a/264;
}



float4 PS(VS_OUTPUT_POS_UV input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 c = screen.SampleLevel(sam1,input.uv,0);
    float fmask = mask(input.uv-float2(0,0.0));
    fmask=pow(fmask,.5)/1.2;

    float n=perlinNoise3D(float3((input.uv-.5)*float2(36,26)+float2(0,-time.x/200.),pow(input.uv.y,1)*2))+.5;
    n=perlinNoise3D(float3((input.uv-.5)*float2(29,16)+float2(0,-time.x/230.+fmask),n))+.5;
    
    n*=pow(input.uv.y,input.uv.y*4.4)*20;
    
        float4 colors[4] ={
            float4(3,3,3,0.0),
            float4(3,2,1,0.31),
            float4(1,0,0,0.71),
            float4(0,0,0,0.99),
        };

        float3 fire=Interpolate4ColorsSmooth((.85-fmask)*n*1.6,colors)/5;
        
        c.rgb*=c.a;
        c.rgb+=fire*fmask*3;
        float fl = dot(fire,float3(1,1,1))*25;
        c.a=max(fmask*fl,c.a);
        
    return c;


}