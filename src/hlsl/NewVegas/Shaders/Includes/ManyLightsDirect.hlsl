// UNOFFICIAL: the sun's and the lamps' light in ObjectTemplate.hlsl's LIGHTS >= 4 pixel shader, pasted into its main()
// twice: once with the metal finish and once without (LIGHT_FINISH, defined by the includer: "roughness" or
// "lightRoughness, metallic"). The copy without metal is the old code exactly, and pixels without metal take it, so metal
// costs nothing where there is none. Uses main()'s locals: IN, viewDir, normal, baseColor, att, lighting, sunShadow
// (without OPT) and the forward point shadows (SHADOWED).

#ifndef OPT
    lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * sunShadow, viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#else
    att = vanillaAtt(PSLightPosition[0].xyz - IN.lPosition.xyz, PSLightPosition[0].w);
    lighting = 0.0f;
    LAMP_IF(att)   // UNOFFICIAL LampReachSkip
        lighting = (0 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.lightDir.xyz, att, SHADOWED(PSLightColor[0].rgb, 0, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

// UNOFFICIAL: SHADOWED(colour, k, att) -- light k of the draw is PSLightColor[k] (k = 0 is the sun's slot without OPT).
// UNOFFICIAL (2026-10-08): light k counts only when lightsUsed > k, as in the decoded vanilla shader (upstream 0f03812a).
// Lights 2-5 were taken at lightsUsed == k (and light 0 under OPT always): the game sets only the used lights'
// constants, so that light was whatever the previous draw left in c3-c26 -- a phantom lamp per object that came and
// went with the draw order (floor pieces lighting up and going dark with hard edges as the camera moved, PBR only).
att = vanillaAtt(PSLightPosition[lightOffset + 0].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 0].w);
LAMP_IF(att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
    lighting += (1 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light2.xyz, att, SHADOWED(PSLightColor[1].rgb, 1, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

att = vanillaAtt(PSLightPosition[lightOffset + 1].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 1].w);
LAMP_IF(att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
    lighting += (2 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light3.xyz, att, SHADOWED(PSLightColor[2].rgb, 2, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

#if MAX_LIGHTS > 3
    att = vanillaAtt(PSLightPosition[lightOffset + 2].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 2].w);
    LAMP_IF(att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
        lighting += (3 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light4.xyz, att, SHADOWED(PSLightColor[3].rgb, 3, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

#if MAX_LIGHTS > 4
    // UNOFFICIAL 2026-10-13: lightOffset, as for the lamps above (missing: in the OPT passes lamps 5 and 6 took the previous
    // lamp's position for their falloff).
    att = vanillaAtt(PSLightPosition[lightOffset + 3].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 3].w);
    LAMP_IF(att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
        lighting += (4 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light5.xyz, att, SHADOWED(PSLightColor[4].rgb, 4, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

    att = vanillaAtt(PSLightPosition[lightOffset + 4].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 4].w);
    LAMP_IF(att)   // UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl): a lamp out of reach adds exactly 0
        lighting += (5 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light6.xyz, att, SHADOWED(PSLightColor[5].rgb, 5, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif
