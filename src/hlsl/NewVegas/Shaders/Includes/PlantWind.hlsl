#ifndef PLANT_WIND_ON
    #define PLANT_WIND_ON 0
#endif
#ifndef CLOTH_WIND_ON
    #define CLOTH_WIND_ON 0
#endif

float PlantLeafGust(float2 P, float2 K, float clock) {
    const float gustPhase = clock * 0.25f;
    const float along = dot(P, K);
    const float front = 0.5f + 0.5f * sin(gustPhase - along);
    const float patch = 0.75f + 0.25f * sin(gustPhase - 0.5f * (along + P.y * K.x - P.x * K.y));
    return front * front * patch;
}

#if PLANT_WIND_ON
#ifdef CLOTH_WIND_DECLARED
    #define PlantA ClothA
    #define PlantB ClothB
    #define PlantC ClothC
    #define PlantD ClothD
    #define PlantE ClothE
    #define PlantF ClothF
#else
    float4 PlantA : register(c248);
    float4 PlantB : register(c249);
    float4 PlantC : register(c250);
    float4 PlantD : register(c251);
    float4 PlantE : register(c252);
    float4 PlantF : register(c253);
#endif

float3 PlantWindMove(float3 p) {
    const float h = dot(p, PlantE.xyz) + PlantE.w;
    float w = saturate(h * PlantA.w);
    w *= w;
    const float front = 0.5f + 0.5f * sin(PlantB.w - dot(p, PlantB.xyz));
    const float patch = 0.75f + 0.25f * sin(PlantC.w - dot(p, PlantC.xyz));
    float2 flutter;
    sincos(PlantD.w + dot(p, PlantD.xyz), flutter.x, flutter.y);
    const float along = (PlantA.y * front * front * patch + PlantA.z * flutter.x) * w;
    const float across = PlantA.z * 0.5f * flutter.y * w;
    const float s2 = dot(PlantE.xyz, PlantE.xyz);
    const float3 side = cross(PlantE.xyz, PlantF.xyz) * rsqrt(s2);
    const float drop = min((along * along + across * across) * 0.5f / max(h, 1.0f), max(h, 0.0f));
    return PlantF.xyz * along + side * across - PlantE.xyz * (drop / s2);
}
#endif
