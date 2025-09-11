cbuffer MatrixBuffer : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
};

cbuffer LightBuffer : register(b1)
{
    float3 lightDirection;
    float3 lightColor;
    float lightIntensity;
    
    float ambientIntensity;
    float3 cameraPosition;
    float specularPower;
    float specularIntensity;
    float2 padding; // 16 byte padding
};

Texture2D catTexture : register(t0);
SamplerState defaultSampler : register(s0);

// vertex Input
struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

// vertex output / pixel input
struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 worldNormal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};