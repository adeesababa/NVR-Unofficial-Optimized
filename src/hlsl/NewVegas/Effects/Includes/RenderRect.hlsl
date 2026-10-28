#ifndef NVR_RENDERRECT_HLSL
#define NVR_RENDERRECT_HLSL

float4 TESR_RenderScale;

float2 rectToScreen(float2 uv) { return uv * TESR_RenderScale.zw; }
float2 screenToRect(float2 s01) { return s01 * TESR_RenderScale.xy; }
float2 clampToRect(float2 uv, float2 texel) { return clamp(uv, 0.5 * texel, TESR_RenderScale.xy - 0.5 * texel); }
float2 clampToRectIfOn(float2 uv, float2 texel) { return (TESR_RenderScale.x < 0.9999 || TESR_RenderScale.y < 0.9999) ? clampToRect(uv, texel) : uv; }
bool outsideRect(float2 uv) { return any(uv < 0.0) || any(uv > TESR_RenderScale.xy); }

#endif
