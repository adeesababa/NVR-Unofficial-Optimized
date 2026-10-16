// PBR calculations.
#if defined(__INTELLISENSE__)
    #include "Helpers.hlsl"
#endif

// Geometric specular AA
// http://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
// https://www.jcgt.org/published/0010/02/02/paper.pdf
//
// Widens roughness where the NORMAL varies fast across a pixel's screen-space footprint --
// exactly what a high-frequency normal map (hair, being the sharpest example NVR ships) does.
// Without this, a GGX peak scales roughly as 1/roughness^4: two adjacent texels whose gloss
// differs by 10 points (0.85 vs 0.95) put out about an 80x difference in peak brightness, pure
// per-texel noise the material texture never intended anyone to resolve. In ObjectTemplate.hlsl
// that noise is the HAIR (ONLY_SPECULAR) pass's own alpha-blend weight, so it showed up as a
// splotchy, moth-eaten alpha pattern rather than as a shimmer in the highlight itself.
float SpecularAA(float3 normal, float roughness) {
    const float SIGMA2 = 0.15915494;
    const float KAPPA = 0.18;
    float3 dndu = ddx(normal);
    float3 dndv = ddy(normal);
    float variance = SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernel_roughness = min(KAPPA, variance);
    return sqrt(saturate(roughness * roughness + kernel_roughness));
}

// UNOFFICIAL 2026-10-16 EnergySplit ([Shaders.PBR.Main], PBR.cpp; live). c223: clear of every other pixel shader constant
// NVR sets. Zero (not set) = off.
float4 TESR_PBRMath : register(c223); // x: EnergySplit (1 = on)

// EnergySplit. The full pass and the sun pass give the diffuse part (1 - F(L.H)) of the light. For a white surface that adds up to
// MORE light than the surface receives: up to +8.9% on smooth surfaces seen at a low angle (roughness 0.07-0.45, N.V 0.06-0.58;
// white furnace, tests\furnace). This is that measured excess as a function of roughness and N.V (fitted to the furnace tables to
// +-0.4%, bench\foundations\REPORT.md) and the diffuse part keeps the rest: surfaces that did not gain keep exactly their light.
// It depends on the view and the roughness only, so the shader works it out once per pixel (SetDiffuseEnergyKeep, before its
// lighting calls) and every lamp reuses it; shaders that do not call it keep the old split (1). 0.95: the diffuse share there.
float DiffuseEnergyKeep(float NdotV, float roughness) {
    const float t = saturate((NdotV - 0.055f) * (1.0f / 0.525f));
    const float x = saturate((0.53f - roughness) * (1.0f / 0.461f));
    const float lowEnd = saturate((roughness - 0.02f) * (1.0f / 0.05f));
    const float u = 1.0f - t;
    const float excess = x * lowEnd * t * u * u * ((0.10380582f + 0.69366243f * x) + t * (0.27762281f - 0.88561283f * x));
    return TESR_PBRMath.x > 0.5f ? 1.0f - excess * (1.0f / 0.95f) : 1.0f;
}
static float PBRDiffuseKeep = 1.0f;
void SetDiffuseEnergyKeep(float3 normal, float3 eyeDir, float roughness) {
    PBRDiffuseKeep = DiffuseEnergyKeep(max(shades(normalize(normal), normalize(eyeDir)), 0.00001), roughness);
}

// Fresnel
// Schlick approximation
float3 Fresnel(float3 f0, float3 f90, float cosine) {
    return f0 + (f90 - f0) * pow(1 - cosine, 5.f);
}

// Diffuse
// Lambert
float3 LambertianDiffuse(float3 albedo, float3 fresnel) {
    return (1 - fresnel) * albedo / PI;
}

float3 DisneyDiffuse(float3 albedo, float roughness, float NdotV, float NdotL, float LdotH) {
    const float linearRoughness = roughness * roughness;
    
    const float energyBias = lerp (0, 0.5 , linearRoughness);
    const float energyFactor = lerp (1.0, 1.0 / 1.51, linearRoughness);
    const float fd90 = energyBias + 2.0 * LdotH * LdotH * linearRoughness;
    const float3 f0 = float(1.0).xxx;
    const float lightScatter = Fresnel(f0, fd90, NdotL).r;
    const float viewScatter = Fresnel(f0, fd90, NdotV).r;

    return (albedo / PI) * lightScatter * viewScatter * energyFactor;
}

// Specular
// D (normal distribution function)
float GGX(float NdotH, float roughness) {
    float alpha = roughness * roughness;
    float a2 = pow(roughness, 4);
    float d = max((NdotH * a2 - NdotH) * NdotH + 1, 1e-5);
    return a2 / (PI * d * d);
}

// G1
float ShlickBeckmann(float NdotX, float roughness) {
    float k = pow(roughness + 1, 2) / 8.0;
    return NdotX/max(NdotX * (1 - k) + k, 0.00000001);
}

// Smith
float GeometryShadowing(float roughness, float NdotV, float NdotL) {
    return ShlickBeckmann(NdotV, roughness) * ShlickBeckmann(NdotL, roughness);
}

// F
float3 FresnelShlick(float3 reflectance, float3 halfway, float3 eyeDir) {
    return reflectance + (1 - reflectance) * pow(1 - shades(halfway, eyeDir), 5.0);
}

// BRDF
float3 BRDF(float roughness, float3 fresnel, float NdotV, float NdotL, float NdotH){
    float3 num = GGX(NdotH, roughness) * GeometryShadowing(roughness, NdotV, NdotL) * fresnel;
    float denom = 4.0 * NdotV * NdotL;
    return num/denom;
}

float3 PBRDiffuse(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    normal = normalize(normal);
    lightDir = normalize(lightDir);

    const float NdotL = shades(normal, lightDir);

    // No Fresnel. These permutations render no specular lobe, so energy taken out of diffuse
    // has nowhere to reappear -- PBRSun returns it as spec * NdotS, this path just loses it.
    // (1 - LdotH)^5 then drives the surface to black as eyeDir approaches -lightDir, where
    // normalize(eyeDir + lightDir) is singular besides. Dropping the term removes the last
    // view dependence, which is what a purely Lambertian material should have.
    const float3 diffuse = (1 - metallicness) * albedo / PI;

    return diffuse * NdotL * lightColor * PI;
}

float3 PBRSpecular(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 reflectance = lerp(float(0.04).rrr, albedo, metallicness);
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 halfway = normalize(eyeDir + lightDir);
    const float NdotL = max(shades(normal, lightDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, (1.0).xxx, LdotH);
    
    const float3 spec = BRDF(roughness, fresnel, NdotV, NdotL, NdotH);

    return spec * NdotL * lightColor * PI;
}

float3 PBR(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 reflectance = lerp(float(0.04).rrr, albedo, metallicness);
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 halfway = normalize(eyeDir + lightDir);
    const float NdotL = max(shades(normal, lightDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, (1.0).xxx, LdotH);
    
    const float3 diffuse = (1 - metallicness) * LambertianDiffuse(albedo, fresnel) * PBRDiffuseKeep;
    
    const float3 spec = BRDF(roughness, fresnel, NdotV, NdotL, NdotH);

    return (diffuse + spec) * NdotL * lightColor * PI;
}

#define SUN_RADIUS 0.00918043

float3 PBRSunSpecular(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 reflectance = lerp(float(0.04).rrr, albedo, metallicness);
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 reflectDir = reflect(lightDir, normal);

    const float radius = sin(SUN_RADIUS);
    const float dist = cos(SUN_RADIUS);
    
    const float3 LdotR = dot(lightDir, reflectDir);
    const float3 closestPoint = reflectDir - LdotR * lightDir;
    // UNOFFICIAL 2026-10-16: closestPoint is zero when the sun is exactly along the normal; normalize() of it was NaN. Elsewhere
    // the same normalize() as before, so the result is bit for bit the old one.
    const float3 closestDir = dot(closestPoint, closestPoint) > 1e-12f ? normalize(closestPoint) : 0.0f;
    const float3 sunDir = LdotR < dist ? normalize(dist * lightDir + closestDir * radius) : reflectDir;
    
    const float3 halfway = normalize(eyeDir + sunDir);
    const float NdotS = max(shades(normal, sunDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float NdotL = shades(normal, lightDir);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, (1.0).xxx, LdotH);
    
    const float3 spec = BRDF(roughness, fresnel, NdotV, NdotS, NdotH);

    return spec * NdotS * lightColor * PI;
}

float3 PBRSun(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 reflectance = lerp(float(0.04).rrr, albedo, metallicness);
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 reflectDir = reflect(lightDir, normal);

    const float radius = sin(SUN_RADIUS);
    const float dist = cos(SUN_RADIUS);
    
    const float3 LdotR = dot(lightDir, reflectDir);
    const float3 closestPoint = reflectDir - LdotR * lightDir;
    // UNOFFICIAL 2026-10-16: closestPoint is zero when the sun is exactly along the normal; normalize() of it was NaN. Elsewhere
    // the same normalize() as before, so the result is bit for bit the old one.
    const float3 closestDir = dot(closestPoint, closestPoint) > 1e-12f ? normalize(closestPoint) : 0.0f;
    const float3 sunDir = LdotR < dist ? normalize(dist * lightDir + closestDir * radius) : reflectDir;
    
    const float3 halfway = normalize(eyeDir + sunDir);
    const float NdotS = max(shades(normal, sunDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float NdotL = shades(normal, lightDir);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, (1.0).xxx, LdotH);
    
    const float3 diffuse = (1 - metallicness) * LambertianDiffuse(albedo, fresnel) * PBRDiffuseKeep;
    
    const float3 spec = BRDF(roughness, fresnel, NdotV, NdotS, NdotH);

    return (diffuse * NdotL + spec * NdotS) * lightColor * PI;
}
