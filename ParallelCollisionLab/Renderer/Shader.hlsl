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
    float4 BallColor;
};

struct VS_INPUT
{
    float3 Position : POSITION;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    float4 worldPos = mul(Model, float4(input.Position, 1.0f));
    output.Position = mul(ViewProj, worldPos);
    output.Color    = BallColor;

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    return input.Color;
}
