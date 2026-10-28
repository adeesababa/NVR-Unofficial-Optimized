#ifndef LINEARLIGHT_INCLUDED
#define LINEARLIGHT_INCLUDED

#ifdef LINEAR_LIGHTING
float4 TESR_LinearLighting : register(c191);

float3 LinColor(float3 c) { return TESR_LinearLighting.x > 1.0f ? pow(max(c, 0.0f), TESR_LinearLighting.x) : c; }
float3 LinEncode(float3 c) { return TESR_LinearLighting.x > 1.0f ? pow(max(c, 0.0f), TESR_LinearLighting.y) : c; }
float LinScale(float s) { return TESR_LinearLighting.x > 1.0f ? pow(max(s, 0.0f), TESR_LinearLighting.x) : s; }
float LinFalloff(float a) {
    const float k = TESR_LinearLighting.z > 0.0f ? TESR_LinearLighting.z : TESR_LinearLighting.x;
    return TESR_LinearLighting.x > 1.0f ? pow(max(a, 0.0f), k) : a;
}
#else
float3 LinColor(float3 c) { return c; }
float3 LinEncode(float3 c) { return c; }
float LinScale(float s) { return s; }
float LinFalloff(float a) { return a; }
#endif

#if defined(LINEAR_LIGHTING) && defined(LINEAR_SCENE_ON) && LINEAR_SCENE_ON
float3 LinearSceneOut(float3 c) { return TESR_LinearLighting.w > 0.5f ? pow(max(c, 0.0f), TESR_LinearLighting.x) : c; }
#else
float3 LinearSceneOut(float3 c) { return c; }
#endif

#if defined(__INTELLISENSE__)
    #include "LampFalloff.hlsl"
#else
    #include "includes/LampFalloff.hlsl"
#endif

#endif
