// UNOFFICIAL: the sun's and the lamps' light in ObjectTemplate.hlsl's LIGHTS >= 4 pixel shader, pasted into its main()
// twice: once with the metal finish and once without (LIGHT_FINISH, defined by the includer: "roughness" or
// "lightRoughness, metallic"). The copy without metal is the old code exactly, and pixels without metal take it, so metal
// costs nothing where there is none. Uses main()'s locals: IN, viewDir, normal, baseColor, att, lighting, sunShadow
// (without OPT) and the forward point shadows (SHADOWED).

#ifdef MANY_LAMPS
// UNOFFICIAL 2026-10-27 ManyLampsOnePass (core/ManyLamps.h, ObjectTemplate.hlsl MANY_LAMPS; after Conor (mrfury97)'s fork, see ManyLamps.h): every lamp of the object from c37-c63,
// its direction taken into tangent space per pixel through the frame's rows (light2/3/4 .xyz) and its falloff from the distance, all in
// the object's own space as the game makes its lamp constants (2026-10-27 first version: camera-relative world space; reached nothing).
// The sun (without OPT) as before.
// Lamp k of c37-c63 is light k + 1 of the game's pass, so the first six keep their forward cube-map shadow slot (SHADOWED: slot k
// with OPT, where light 0 is not in the shader, k + 1 without); lamps the game did not keep in the pass (k past its count) and
// lamps 7-13 have none (their slots' lookups stay 1: see the INTERIOR_SHADOWS block in ObjectTemplate.hlsl).
#ifndef OPT
    lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * sunShadow, viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#else
    lighting = 0.0f;
#endif
{
    const float3 mlWorldPos = IN.lPosition.xyz;	// the object's space, as the lamps in c38 + 2k
    const float mlCount = NVR_ManyLamps[0].x;
    #define ML_TS(d) float3(dot(IN.light2.xyz, (d)), dot(IN.light3.xyz, (d)), dot(IN.light4.xyz, (d)))
    // (A lamp without reach, or a pixel whose frame is all zero, adds nothing: no division by 0, no normalising a zero vector.)
    #define ML_LAMP_AT(COND, POSITION, COLOR) [branch] if (COND) { \
            const float4 mlP = POSITION; \
            const float3 mlD = mlP.xyz - mlWorldPos; \
            const float3 mlL = ML_TS(mlD); \
            att = vanillaAtt(mlD, max(mlP.w, 1.0e-3f)); \
            [branch] if (att > 0.0f && mlP.w > 0.0f && dot(mlL, mlL) > 1.0e-8f) \
                lighting += getPointLightLightingAtt(mlL, att, COLOR, viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH); \
        }
    #define ML_LAMP(j, COLOR) ML_LAMP_AT((j) < mlCount, NVR_ManyLamps[1 + 2 * (j)], COLOR)
    #ifdef OPT
        ML_LAMP(0, SHADOWED(NVR_ManyLamps[2].rgb, 0, att))
        ML_LAMP(1, SHADOWED(NVR_ManyLamps[4].rgb, 1, att))
        ML_LAMP(2, SHADOWED(NVR_ManyLamps[6].rgb, 2, att))
        ML_LAMP(3, SHADOWED(NVR_ManyLamps[8].rgb, 3, att))
        ML_LAMP(4, SHADOWED(NVR_ManyLamps[10].rgb, 4, att))
        ML_LAMP(5, SHADOWED(NVR_ManyLamps[12].rgb, 5, att))
    #else
        ML_LAMP(0, SHADOWED(NVR_ManyLamps[2].rgb, 1, att))
        ML_LAMP(1, SHADOWED(NVR_ManyLamps[4].rgb, 2, att))
        ML_LAMP(2, SHADOWED(NVR_ManyLamps[6].rgb, 3, att))
        ML_LAMP(3, SHADOWED(NVR_ManyLamps[8].rgb, 4, att))
        ML_LAMP(4, SHADOWED(NVR_ManyLamps[10].rgb, 5, att))
        ML_LAMP(5, NVR_ManyLamps[12].rgb)
    #endif
    [loop] for (int mlIndex = 6; mlIndex < MANY_LAMPS_MAX; mlIndex++) {
        [branch] if (mlIndex >= mlCount) break;
        ML_LAMP(mlIndex, NVR_ManyLamps[2 + 2 * mlIndex].rgb)
    }
    // 2026-10-27: lamps 14-24 from the two other ranges (ObjectTemplate.hlsl NVR_ManyLampsB / C).
    [branch] if (mlCount > MANY_LAMPS_MAX) {
        [loop] for (int mlB = 0; mlB < MANY_LAMPS_B; mlB++) {
            [branch] if (MANY_LAMPS_MAX + mlB >= mlCount) break;
            ML_LAMP_AT(true, NVR_ManyLampsB[2 * mlB], NVR_ManyLampsB[2 * mlB + 1].rgb)
        }
        [loop] for (int mlC = 0; mlC < MANY_LAMPS_C; mlC++) {
            [branch] if (MANY_LAMPS_MAX + MANY_LAMPS_B + mlC >= mlCount) break;
            ML_LAMP_AT(true, NVR_ManyLampsC[2 * mlC], NVR_ManyLampsC[2 * mlC + 1].rgb)
        }
    }
    #undef ML_LAMP
    #undef ML_LAMP_AT
    #undef ML_TS
}
#else
#ifndef OPT
    lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * sunShadow, viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#else
    att = vanillaAtt(PSLightPosition[0].xyz - IN.lPosition.xyz, PSLightPosition[0].w);
    lighting = 0.0f;
    LAMP_IFK(0, att)   // UNOFFICIAL LampReachSkip
        lighting = (0 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.lightDir.xyz, att, SHADOWED(PSLightColor[0].rgb, 0, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

// UNOFFICIAL: SHADOWED(colour, k, att) -- light k of the draw is PSLightColor[k] (k = 0 is the sun's slot without OPT).
// UNOFFICIAL (2026-10-08): light k counts only when lightsUsed > k, as in the decoded vanilla shader (upstream 0f03812a).
// Lights 2-5 were taken at lightsUsed == k (and light 0 under OPT always): the game sets only the used lights'
// constants, so that light was whatever the previous draw left in c3-c26 -- a phantom lamp per object that came and
// went with the draw order (floor pieces lighting up and going dark with hard edges as the camera moved, PBR only).
att = vanillaAtt(PSLightPosition[lightOffset + 0].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 0].w);
LAMP_IFK(1, att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
    lighting += (1 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light2.xyz, att, SHADOWED(PSLightColor[1].rgb, 1, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

att = vanillaAtt(PSLightPosition[lightOffset + 1].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 1].w);
LAMP_IFK(2, att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
    lighting += (2 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light3.xyz, att, SHADOWED(PSLightColor[2].rgb, 2, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

#if MAX_LIGHTS > 3
    att = vanillaAtt(PSLightPosition[lightOffset + 2].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 2].w);
    LAMP_IFK(3, att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
        lighting += (3 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light4.xyz, att, SHADOWED(PSLightColor[3].rgb, 3, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

#if MAX_LIGHTS > 4
    // UNOFFICIAL 2026-10-13: lightOffset, as for the lamps above (missing: in the OPT passes lamps 5 and 6 took the previous
    // lamp's position for their falloff).
    att = vanillaAtt(PSLightPosition[lightOffset + 3].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 3].w);
    LAMP_IFK(4, att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
        lighting += (4 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light5.xyz, att, SHADOWED(PSLightColor[4].rgb, 4, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

    att = vanillaAtt(PSLightPosition[lightOffset + 4].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 4].w);
    LAMP_IFK(5, att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
        lighting += (5 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light6.xyz, att, SHADOWED(PSLightColor[5].rgb, 5, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif
#endif // MANY_LAMPS
