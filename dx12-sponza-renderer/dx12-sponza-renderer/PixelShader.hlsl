#include "Constants.hlsl"

static const float3 DEFAULT_COLOR = float3(0.8, 0.8, 0.8);
float4 main(PS_INPUT input) : SV_Target
{
    float4 textureColor = float4(DEFAULT_COLOR, 1.0);
    
    float3 normal = normalize(input.worldNormal);
    
    // FROM surface TO light
    float3 lightDir = normalize(-lightDirection);
    
    // view vector from surface to camera
    float3 viewDir = normalize(cameraPosition - input.worldPosition);
    
    // reflection vector
    float3 reflectDir = reflect(-lightDir, normal);
    
    // specular component for phong model
    float specular = pow(max(0, dot(viewDir, reflectDir)), specularPower);
    specular *= specularIntensity;
    
    // lambert diffuse
    float ndotl = max(0, dot(normal, lightDir));
    float3 diffuse = lightColor * lightIntensity * ndotl;
    
    // base ambient
    float3 ambient = lightColor * ambientIntensity;
    
    float3 finalColor = textureColor.rgb * (ambient + diffuse + specular);
    
    return float4(finalColor,1.0f);
}