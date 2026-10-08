#ifndef OPT
    lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * sunShadow, viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#else
    att = vanillaAtt(PSLightPosition[0].xyz - IN.lPosition.xyz, PSLightPosition[0].w);
    lighting = (0 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.lightDir.xyz, att, SHADOWED(PSLightColor[0].rgb, 0, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

att = vanillaAtt(PSLightPosition[lightOffset + 0].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 0].w);
lighting += (1 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light2.xyz, att, SHADOWED(PSLightColor[1].rgb, 1, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

att = vanillaAtt(PSLightPosition[lightOffset + 1].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 1].w);
lighting += (2 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light3.xyz, att, SHADOWED(PSLightColor[2].rgb, 2, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

#if MAX_LIGHTS > 3
    att = vanillaAtt(PSLightPosition[lightOffset + 2].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 2].w);
    lighting += (3 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light4.xyz, att, SHADOWED(PSLightColor[3].rgb, 3, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif

#if MAX_LIGHTS > 4
    att = vanillaAtt(PSLightPosition[3].xyz - IN.lPosition.xyz, PSLightPosition[3].w);
    lighting += (4 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light5.xyz, att, SHADOWED(PSLightColor[4].rgb, 4, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);

    att = vanillaAtt(PSLightPosition[4].xyz - IN.lPosition.xyz, PSLightPosition[4].w);
    lighting += (5 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(IN.light6.xyz, att, SHADOWED(PSLightColor[5].rgb, 5, att), viewDir.xyz, normal.xyz, baseColor.rgb, LIGHT_FINISH);
#endif
