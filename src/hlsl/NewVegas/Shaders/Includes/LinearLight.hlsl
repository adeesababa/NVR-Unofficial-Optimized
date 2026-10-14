#ifndef LINEARLIGHT_INCLUDED
#define LINEARLIGHT_INCLUDED

#ifdef LINEAR_LIGHTING
float4 TESR_LinearLighting : register(c191);

float3 LinColor(float3 c) { return TESR_LinearLighting.x > 1.0f ? pow(max(c, 0.0f), TESR_LinearLighting.x) : c; }
float3 LinEncode(float3 c) { return TESR_LinearLighting.x > 1.0f ? pow(max(c, 0.0f), TESR_LinearLighting.y) : c; }
float LinScale(float s) { return TESR_LinearLighting.x > 1.0f ? pow(max(s, 0.0f), TESR_LinearLighting.x) : s; }
#else
float3 LinColor(float3 c) { return c; }
float3 LinEncode(float3 c) { return c; }
float LinScale(float s) { return s; }
#endif

#endif
