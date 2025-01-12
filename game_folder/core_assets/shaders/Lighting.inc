


struct Light
{
  float4 pos;
  float4 color;

  // spot
  float4 direction;
  float  innerCone;
  float  outerCone;
  float  invConeDifference;
  float  invLightRadius;
};


float GetDistanceAttenuation(float distance, float invLightRadius)
{
    float l = saturate(1.0f - (distance * invLightRadius));
    return l * l;
}


float4 Diffuse(float4 radiance, float3 n, float3 l, float4 albedo)
{
  float d = saturate(dot(n, l));
  return d * albedo * radiance;
}

float4 BlinnPhong(float4 radiance, float3 n, float3 l, float3 v,
  float4 albedo, float4 specular, float shininess)
{
  float  d = saturate(dot(n, l));
  float3 h = normalize(l + v);

  float4 s = pow(saturate(dot(n, h)), shininess + 0.0001) * specular * saturate(d * 10.0);
  return (d * albedo + s) * radiance;
}


float4 ComputeSpotLight(float3 viewPosition, float3 vsNormal, float4 albedo,
  Light light)
{
    float3 l = light.pos.xyz - viewPosition;
    float d = length(l);
    l = l / d;

    float att = GetDistanceAttenuation(d, light.invLightRadius);

    float theta = dot(l, light.direction.xyz);
    float intensity = saturate((theta - light.outerCone) * light.invConeDifference);

    att *= intensity;

    float4 radiance = light.color * att;

#if 0
    // Compute the normalized view direction.
    float3 v = normalize(-viewPosition);

    float4 specularGloss = float4(1,1,1,1);
    float4 col = BlinnPhong(radiance, vsNormal, l, v, albedo, specularGloss, specularGloss.a * 256);
#else
    float4 col = Diffuse(radiance, vsNormal, l, albedo);
#endif
    return col;
}

float4 ComputePointLight(float3 viewPosition, float3 vsNormal, float4 albedo,
  Light light)
{
    float3 l = light.pos.xyz - viewPosition;
    float d = length(l);
    l = l / d;

    float att = GetDistanceAttenuation(d, light.invLightRadius);

    float4 radiance = light.color * att;

#if 0
    // Compute the normalized view direction.
    float3 v = normalize(-viewPosition);

    float4 specularGloss = float4(1,1,1,0.4);
    float4 col = BlinnPhong(radiance, vsNormal, l, v, albedo, specularGloss, specularGloss.a * 256);
#else
    float4 col = Diffuse(radiance, vsNormal, l, albedo);
#endif
    return col;
}

// -----------------------------------------------------

#define PI 3.1415926535897932384626433832795f
#define GAMMA 2.2f

// NDF / D - Normal Distribution Function
// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2.
float NormalDistributionGGX(float cosLh, float roughness)
{
  float alpha   = roughness * roughness;
  float alphaSq = alpha * alpha;

  float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
  return alphaSq / (PI * denom * denom);
}


// Single term for separable Schlick-GGX below.
float __gaSchlickG1(float cosTheta, float k)
{
  return cosTheta / (cosTheta * (1.0 - k) + k);
}

// G - Geometry shadowing term
// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float GeometrySchlickGGX(float cosLi, float cosLo, float roughness)
{
  // Epic suggests using this roughness remapping for analytic lights.
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;

  return __gaSchlickG1(cosLi, k) * __gaSchlickG1(cosLo, k);
}

// F - Fresnel factor called
// Shlick's approximation of the Fresnel factor.
float3 FresnelSchlick(float3 F0, float cosTheta)
{
  return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}