
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

// UNOFFICIAL 2026-10-26: the true world direction of a sky pixel, for SkyLook's horizon fade. The sky shaders' own eye direction is
// the dome mesh's vertex position, and the dome sits so that even the visible horizon has a large up component (the horizon tint
// never showed). Rebuilt from the pixel and the camera: the shader declares TESR_ReciprocalResolution, TESR_InvProjectionTransform
// and TESR_InvViewTransform (row_major). Only the main view's camera: in the water reflection the fade is approximate.
#ifdef SKY_VIEW_DIRECTION
float3 SkyViewDirection(float2 vpos) {
    float2 ndc = float2((vpos.x + 0.5f) * TESR_ReciprocalResolution.x * 2.0f - 1.0f, 1.0f - (vpos.y + 0.5f) * TESR_ReciprocalResolution.y * 2.0f);
    float4 view = mul(float4(ndc, 0.5f, 1.0f), TESR_InvProjectionTransform);
    return normalize(mul(float4(view.xyz / view.w, 0.0f), TESR_InvViewTransform).xyz);
}
#endif

// UNOFFICIAL 2026-10-26 [Shaders.Sky.Main] sky look, for SKY.pso and the sky the flat clouds blend with (SKYTEX.pso). Sky.cpp SkyLook
// mirrors it for the sky light on objects: change one and change the other. Saturation first (look.x, 1 = no change), then the tint,
// which is the main tint times the horizon tint at the horizon, fading to the main tint alone at look.y (TintGradientHeight, the view
// direction's up component). With TintKeepsBrightness on, Sky.cpp hands both tints over already divided by their own brightness.
float3 SkyLook(float3 color, float up, float3 tint, float3 horizonTint, float4 look) {
    if (look.x != 1.0f) color = max(lerp(luma(color).xxx, color, look.x), 0.0f);
    float overhead = smoothstep(0.0f, look.y, saturate(up));
    return color * tint * lerp(horizonTint, 1.0f, overhead);
}