#include "Constants.hlsl"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    float4 viewPos = mul(worldPos, view);
    output.position = mul(viewPos, projection);

    output.worldPosition = worldPos.xyz;
    
    output.worldNormal = mul(input.normal, (float3x3) world);
    output.normal = input.normal;
    output.texcoord = input.texcoord;
    
    return output;
}