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

float4 TESR_PBRMath : register(c223);

float3 ReflectivityF0(float3 f0) { return saturate(f0 + f0 * TESR_PBRMath.y); }
#define REFLECTIVITY_F90 saturate(1.0f + TESR_PBRMath.y).xxx

#if defined(PBR_SPEC_FADE) || defined(NVR_FOLD)
float4 NVR_SpecFade : register(c211);
#endif

#ifndef PBR_SMITH_CORRELATED
    #define PBR_SMITH_CORRELATED 0
#endif
#if PBR_SMITH_CORRELATED
float SmithCorrelatedKeep(float NdotV, float roughness) {
    const float r = max(roughness, 0.056f);
    const float s = sqrt(clamp(NdotV, 0.02f, 1.0f));
    const float p0 = 0.8720785f + s * (2.377077f + s * (-15.78768f + s * (20.7581f + s * (-8.469884f + s * (0.2183369f)))));
    const float p1 = -2.350215f + s * (6.164611f + s * (12.53951f + s * (-24.69559f + s * (9.321004f))));
    const float p2 = -4.584592f + s * (-17.91134f + s * (14.6047f + s * (0.8975321f)));
    const float p3 = 20.67309f + s * (6.171803f + s * (-8.573712f));
    const float p4 = -22.05094f + s * (2.260031f);
    const float p5 = 7.527356f;
    const float excess = p0 + r * (p1 + r * (p2 + r * (p3 + r * (p4 + r * p5))));
    const float clampLoss = 1.0f - min(1e5f * roughness * roughness * roughness * roughness, 1.0f);
    return 1.0f - saturate(excess * (1.0f - clampLoss * clampLoss));
}
#endif

float DiffuseEnergyKeep(float NdotV, float roughness) {
#if PBR_SMITH_CORRELATED == 2
    return 1.0f + (SmithCorrelatedKeep(NdotV, roughness) - 1.0f) * TESR_PBRMath.x;
#elif PBR_SMITH_CORRELATED
    return SmithCorrelatedKeep(NdotV, roughness);
#else
    const float t = saturate((NdotV - 0.055f) * (1.0f / 0.525f));
    const float x = saturate((0.53f - roughness) * (1.0f / 0.461f));
    const float lowEnd = saturate((roughness - 0.02f) * (1.0f / 0.05f));
    const float u = 1.0f - t;
    const float excess = x * lowEnd * t * u * u * ((0.10380582f + 0.69366243f * x) + t * (0.27762281f - 0.88561283f * x));
    return TESR_PBRMath.x > 0.5f ? 1.0f - excess * (1.0f / 0.95f) : 1.0f;
#endif
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

float3 EnvBRDFApprox(float3 f0, float roughness, float NdotV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * ab.x + ab.y;
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
#if PBR_SMITH_CORRELATED
    const float a2 = pow(roughness, 4);
    const float lambdaV = NdotL * sqrt(NdotV * NdotV * (1 - a2) + a2);
    const float lambdaL = NdotV * sqrt(NdotL * NdotL * (1 - a2) + a2);
    #if PBR_SMITH_CORRELATED == 2
        const float3 oldTerm = GGX(NdotH, roughness) * GeometryShadowing(roughness, NdotV, NdotL) * fresnel / (4.0 * NdotV * NdotL);
        return oldTerm + (GGX(NdotH, roughness) * (0.5 / (lambdaV + lambdaL)) * fresnel - oldTerm) * TESR_PBRMath.x;
    #else
        return GGX(NdotH, roughness) * (0.5 / (lambdaV + lambdaL)) * fresnel;
    #endif
#else
    float3 num = GGX(NdotH, roughness) * GeometryShadowing(roughness, NdotV, NdotL) * fresnel;
    float denom = 4.0 * NdotV * NdotL;
    return num/denom;
#endif
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
    const float3 reflectance = ReflectivityF0(lerp(float(0.04).rrr, albedo, metallicness));
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 halfway = normalize(eyeDir + lightDir);
    const float NdotL = max(shades(normal, lightDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, REFLECTIVITY_F90, LdotH);

    float3 spec = BRDF(roughness, fresnel, NdotV, NdotL, NdotH);
#ifdef PBR_SPEC_FADE
    spec *= NVR_SpecFade.x;
#endif

    return spec * NdotL * lightColor * PI;
}

float3 PBR(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 reflectance = ReflectivityF0(lerp(float(0.04).rrr, albedo, metallicness));
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 halfway = normalize(eyeDir + lightDir);
    const float NdotL = max(shades(normal, lightDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, REFLECTIVITY_F90, LdotH);
    
    float3 diffuse = (1 - metallicness) * LambertianDiffuse(albedo, fresnel) * PBRDiffuseKeep;

    float3 spec = BRDF(roughness, fresnel, NdotV, NdotL, NdotH);
#ifdef PBR_SPEC_FADE
    #ifndef PBR_SPEC_FADE_HIGHLIGHT_ONLY
    diffuse += ((1 - metallicness) * albedo / PI - diffuse) * (1.0f - NVR_SpecFade.x);
    #endif
    spec *= NVR_SpecFade.x;
#endif

    return (diffuse + spec) * NdotL * lightColor * PI;
}

#define SUN_RADIUS 0.00918043

float3 PBRSunSpecular(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 reflectance = ReflectivityF0(lerp(float(0.04).rrr, albedo, metallicness));
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 reflectDir = reflect(lightDir, normal);

    const float radius = sin(SUN_RADIUS);
    const float dist = cos(SUN_RADIUS);
    
    const float3 LdotR = dot(lightDir, reflectDir);
    const float3 closestPoint = reflectDir - LdotR * lightDir;
    const float3 closestDir = dot(closestPoint, closestPoint) > 1e-12f ? normalize(closestPoint) : 0.0f;
    const float3 sunDir = LdotR < dist ? normalize(dist * lightDir + closestDir * radius) : reflectDir;
    
    const float3 halfway = normalize(eyeDir + sunDir);
    const float NdotS = max(shades(normal, sunDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float NdotL = shades(normal, lightDir);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, REFLECTIVITY_F90, LdotH);

    float3 spec = BRDF(roughness, fresnel, NdotV, NdotS, NdotH);
#ifdef PBR_SPEC_FADE
    spec *= NVR_SpecFade.x;
#endif

    return spec * NdotS * lightColor * PI;
}

float3 PBRSun(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 reflectance = ReflectivityF0(lerp(float(0.04).rrr, albedo, metallicness));
    
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);
    
    const float3 reflectDir = reflect(lightDir, normal);

    const float radius = sin(SUN_RADIUS);
    const float dist = cos(SUN_RADIUS);
    
    const float3 LdotR = dot(lightDir, reflectDir);
    const float3 closestPoint = reflectDir - LdotR * lightDir;
    const float3 closestDir = dot(closestPoint, closestPoint) > 1e-12f ? normalize(closestPoint) : 0.0f;
    const float3 sunDir = LdotR < dist ? normalize(dist * lightDir + closestDir * radius) : reflectDir;
    
    const float3 halfway = normalize(eyeDir + sunDir);
    const float NdotS = max(shades(normal, sunDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float NdotL = shades(normal, lightDir);
    const float LdotH = shades(lightDir, halfway);

    const float3 fresnel = Fresnel(reflectance, REFLECTIVITY_F90, LdotH);
    
    float3 diffuse = (1 - metallicness) * LambertianDiffuse(albedo, fresnel) * PBRDiffuseKeep;

    float3 spec = BRDF(roughness, fresnel, NdotV, NdotS, NdotH);
#ifdef PBR_SPEC_FADE
    #ifndef PBR_SPEC_FADE_HIGHLIGHT_ONLY
    diffuse += ((1 - metallicness) * albedo / PI - diffuse) * (1.0f - NVR_SpecFade.x);
    #endif
    spec *= NVR_SpecFade.x;
#endif

    return (diffuse * NdotL + spec * NdotS) * lightColor * PI;
}
