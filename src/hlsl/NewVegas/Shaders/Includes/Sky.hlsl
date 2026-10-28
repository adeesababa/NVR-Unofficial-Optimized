
float3 GetSunColor(float sunHeight, float athmosphere, float dayTime, float3 sunColor, float3 sunsetColor ){
    sunColor = linearize(sunColor);
    sunsetColor = linearize(sunsetColor);
    
    float3 color = (1 + sunHeight) * sunColor; // increase suncolor strength with sun height
    float sunSet = saturate(pows(1 - sunHeight, 8.0)) * dayTime;
    color += sunsetColor.rgb * sunSet * athmosphere; // add extra red to the sun at sunsets
    return color;
}

float3 GetSkyColor(float verticality, float athmosphere, float sunHeight, float sunInfluence, float sunStrength, float3 skyColor, float3 skyLowColor, float3 horizonColor, float3 sunColor){
    skyColor = linearize(skyColor);
    skyLowColor = linearize(skyLowColor);
    horizonColor = linearize(horizonColor);

    float isDayTime = smoothstep(0, 0.5, TESR_SunAmount.x);

    float3 color = lerp(skyLowColor.rgb, skyColor.rgb, verticality); // fade from low sky to high sky with height
    color = lerp(color, horizonColor.rgb, saturate(athmosphere * (0.5 + sunInfluence * 0.5))); // fade from base color to horizon color in athmosphere (stronger on the sun side)
    color += lerp(0.0, sunColor * sunInfluence * (1.0 - sunHeight) * (athmosphere) * sunStrength, isDayTime);

    return color;
}

#ifdef SKY_VIEW_DIRECTION
float3 SkyViewDirection(float2 vpos) {
    float2 s01 = (vpos + 0.5f) * TESR_ReciprocalResolution.xy * TESR_RenderScale.zw;
    float2 ndc = float2(s01.x * 2.0f - 1.0f, 1.0f - s01.y * 2.0f);
    float4 view = mul(float4(ndc, 0.5f, 1.0f), TESR_InvProjectionTransform);
    return normalize(mul(float4(view.xyz / view.w, 0.0f), TESR_InvViewTransform).xyz);
}
#endif

float3 SkyLook(float3 color, float up, float3 tint, float3 horizonTint, float4 look) {
    if (look.x != 1.0f) color = max(lerp(luma(color).xxx, color, look.x), 0.0f);
    float overhead = smoothstep(0.0f, look.y, saturate(up));
    return color * tint * lerp(horizonTint, 1.0f, overhead);
}