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
    int                bUseTexture; // 0: Solid color, 1: Textured (Player)
    float3             Padding;
};

Texture2D    g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

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
    if (bUseTexture != 0)
    {
        return g_Texture.Sample(g_Sampler, input.TexCoord);
    }
    return input.Color;
}
