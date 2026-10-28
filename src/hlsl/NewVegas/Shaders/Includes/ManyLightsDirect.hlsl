
#ifdef MANY_LAMPS
#ifndef OPT
    lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * sunShadow, viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#else
    lighting = 0.0f;
#endif
{
    const float3 mlWorldPos = IN.lPosition.xyz;
    const float mlCount = NVR_ManyLamps[0].x;
    #define ML_TS(d) float3(dot(IN.light2.xyz, (d)), dot(IN.light3.xyz, (d)), dot(IN.light4.xyz, (d)))
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
    LAMP_IFK(0, att)
        lighting = (0 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.lightDir.xyz, att, SHADOWED(PSLightColor[0].rgb, 0, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

att = vanillaAtt(PSLightPosition[lightOffset + 0].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 0].w);
LAMP_IFK(1, att)
    lighting += (1 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light2.xyz, att, SHADOWED(PSLightColor[1].rgb, 1, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

att = vanillaAtt(PSLightPosition[lightOffset + 1].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 1].w);
LAMP_IFK(2, att)
    lighting += (2 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light3.xyz, att, SHADOWED(PSLightColor[2].rgb, 2, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

#if MAX_LIGHTS > 3
    att = vanillaAtt(PSLightPosition[lightOffset + 2].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 2].w);
    LAMP_IFK(3, att)
        lighting += (3 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light4.xyz, att, SHADOWED(PSLightColor[3].rgb, 3, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

#if MAX_LIGHTS > 4
    att = vanillaAtt(PSLightPosition[lightOffset + 3].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 3].w);
    LAMP_IFK(4, att)
        lighting += (4 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light5.xyz, att, SHADOWED(PSLightColor[4].rgb, 4, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

    att = vanillaAtt(PSLightPosition[lightOffset + 4].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 4].w);
    LAMP_IFK(5, att)
        lighting += (5 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light6.xyz, att, SHADOWED(PSLightColor[5].rgb, 5, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif
#endif
