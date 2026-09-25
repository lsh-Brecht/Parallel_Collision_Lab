//=============================================================================
// Shader.hlsl
//=============================================================================
cbuffer PerFrame : register(b0)
{
    row_major float4x4 ViewProj;
};

cbuffer PerObject : register(b1)
{
    row_major float4x4 Model;
    float4             BallColor;
    int                RenderMode; // 0: None, 1: Earth, 2: Mars, 3: UVMap
    float3             Padding;
};

Texture2D    g_TextureEarth : register(t0);
Texture2D    g_TextureMars  : register(t1);
SamplerState g_Sampler      : register(s0);

struct VS_INPUT
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD0;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    float4 worldPos = mul(Model, float4(input.Position, 1.0f));
    output.Position = mul(ViewProj, worldPos);
    output.Color    = BallColor;
    output.TexCoord = input.TexCoord;

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    if (RenderMode == 1)
    {
        return g_TextureEarth.Sample(g_Sampler, input.TexCoord);
    }
    else if (RenderMode == 2)
    {
        return g_TextureMars.Sample(g_Sampler, input.TexCoord);
    }
    else if (RenderMode == 3)
    {
        return float4(input.TexCoord.x, input.TexCoord.y, 0.0f, 1.0f);
    }
    return input.Color;
}
