// Skin lighting helpers.
// Must not reference TESR_DebugVar: it carries whatever the Debug effect is set to.
#ifndef SKIN_FACEGEN_BLEND
    #define SKIN_FACEGEN_BLEND 1.0f      // 0 = raw base texture, 1 = full FaceGen complexion
#endif
#ifndef SKIN_SPECULAR_STRENGTH
    #define SKIN_SPECULAR_STRENGTH 0.0f   // 0 = vanilla (vanilla skin has no specular term)
#endif

float HalfLambert(float3 Vector1, float3 Vector2) {
	
	float product = dot(Vector1, Vector2);
	product *= 0.5;
	product += 0.5;
	return product;
	
}

float BlinnPhongSpecular(float3 Normal, float3 LightDir) {
	
	float3 halfAngle = Normal + LightDir;
	return pow(saturate(dot(Normal, halfAngle)), TESR_SkinData.y);
	
}

float3 Skin(float3 SkinColor, float3 LightColor, float3 CameraDir, float3 LightDir, float3 Normal) {

	float4 dotLN = HalfLambert(LightDir, Normal) * TESR_SkinData.x;
	float3 indirectLightComponent  = TESR_SkinData.z * max(0, dot(-Normal, LightDir));
	
	indirectLightComponent += TESR_SkinData.z * HalfLambert(-CameraDir, LightDir);
	indirectLightComponent *= TESR_SkinData.x;
	indirectLightComponent *= pow(SkinColor, 2);

	float3 rim = (float3)(1.0f - max(0.0f, dot(Normal, CameraDir)));
	rim = pow(rim, 3);
	rim *= max(0.0f, dot(Normal, LightDir)) * LightColor;
	rim *= TESR_SkinData.w;

	float4 finalCol = dotLN * 0.5 + float4(indirectLightComponent, 1.0f);

	finalCol.rgb += finalCol.a * TESR_SkinData.x * rim;
	finalCol.rgb += finalCol.a * TESR_SkinData.x * BlinnPhongSpecular(Normal, LightDir) * TESR_SkinColor.rgb * 0.05f;
	finalCol.rgb *= LightColor;
	
	return finalCol.rgb;
	
};


float3 GetLighting(float3 lightDirection, float3 eyeDirection, float3 normal, float3 lightColor){
    float fresnel = sqr(1 - shades(normal, eyeDirection)) * shades(lightDirection, -eyeDirection) * 0.5; // vanilla fresnel that shows when light is behind

    // float fresnelCoeff = pows(1 - shades(normal, eyeDir), 5);
    float diffuse = shades(normal, lightDirection);
    // float3 fresnel = fresnelCoeff * lightColor * 0.5 * pow(diffuse, TESR_DebugVar.w);
    float3 lighting = diffuse * lightColor + fresnel * lightColor;
    return max(lighting, 0);
}

// UNOFFICIAL: per-pixel sun diffusion, used when PerPixelDiffusion ([Shaders.Skin.Main], TESR_SkinDiffusion.x) is above 0.
// Same tangent-space inputs as GetLighting. Returns the sun term with a wrapped terminator, red bending furthest into the
// shadow side, and thin-part transmission (ears, nostrils) where the sun is behind the surface and the eye looks through it
// toward the sun, scaled by Translucency. The caller blends it with GetLighting by PerPixelDiffusion, then applies the sun
// shadow as usual, so the transmission is shadowed like the rest of the sun.
float3 GetSunDiffusion(float3 lightDirection, float3 eyeDirection, float3 normal, float3 lightColor, float diffusion, float translucency){
    float NdotL = dot(normal, lightDirection);

    // Wrapped diffuse per channel, (N.L + w) / (1 + w): the lit side keeps its brightness.
    float3 wrapAmount = float3(0.45f, 0.25f, 0.15f) * diffusion;
    float3 diffuse = saturate((NdotL + wrapAmount) / (1 + wrapAmount));

    float fresnel = sqr(1 - shades(normal, eyeDirection)) * shades(lightDirection, -eyeDirection) * 0.5; // as GetLighting

    // Thin parts: only where the sun is behind the surface (N.L below 0.2), strongest when the eye looks straight at it.
    float3 transmission = translucency * pow(shades(eyeDirection, -lightDirection), 4) * saturate(-NdotL + 0.2f) * float3(1.0f, 0.35f, 0.2f);

    return max((diffuse + fresnel + transmission) * lightColor, 0);
}

float GetSpecular(float3 lightDirection, float3 eyeDirection, float3 normal, float3 lightColor){
    return  pow(shades(normal, normalize(lightDirection + eyeDirection)), TESR_SkinData.y) * luma(lightColor) * SKIN_SPECULAR_STRENGTH;
}

float GetSSS(float3 lightDirection, float3 normal){
    return (1 - shades(normal, lightDirection)) * 0.5;//TESR_DebugVar.x;
}

float3 getNormal(float2 uv){
    return (expand(tex2D(NormalMap, uv).xyz));
}

// float2 getScreenpos(VS_INPUT IN){
//     return IN.position.xy * TESR_ReciprocalResolution.xy;
// };


float3 ApplyVertexColor(float3 baseColor, float3 vertexColor, float4 toggles){
   return toggles.x <= 0.0 ? baseColor : (baseColor.rgb * vertexColor); // apply vertex color
}

float3 ApplyFog(float3 baseColor, float4 fogColor, float4 toggles){
    return toggles.y <= 0.0 ? baseColor : ((fogColor.a * (fogColor.rgb - baseColor)) + baseColor);
}

float4 getBaseColor(float2 uv, sampler2D FaceGenMap0Buffer, sampler2D FaceGenMap1Buffer, sampler2D BaseColorBuffer){

    float3 faceGenMap0 = tex2D(FaceGenMap0Buffer, uv).rgb;
    float3 faceGenMap1 = tex2D(FaceGenMap1Buffer, uv).rgb;
    float4 baseTexture = tex2D(BaseColorBuffer, uv);
    float4 baseColor = float4(2 * ((expand(faceGenMap0) + baseTexture.rgb) * (2 * faceGenMap1)), baseTexture.a);

	baseColor = lerp(baseTexture, baseColor, SKIN_FACEGEN_BLEND);
    return baseColor;
}


float3 getPointLight(float3 LightDirection, float3 eyeDirection, float3 LightColor, float3 glowTexture, float3 normal, float Attenuation1, float Attenuation2){
    float4 SSScolor;
    SSScolor.rgb = lerp(LightColor, glowTexture, 0.5);

    float fresnel = sqr(1 - shades(normal, eyeDirection));
    //SSScolor.a = (AmbientColor.a >= 1 ? 0 : (baseColor.a - Toggles.w)); // alpha flag?

    float diffuse = dot(normal, LightDirection);
    float diffuse2 = saturate((diffuse + 0.3) * 0.769230783); // ?
    diffuse = saturate(diffuse);

    float3 pointLightContribution = saturate(((3 - diffuse2 * 2) * sqr(diffuse2)) - ((3 - (diffuse * 2)) * sqr(diffuse))) * glowTexture;
    pointLightContribution += diffuse * LightColor;
    pointLightContribution += fresnel * shades(eyeDirection, -LightDirection) * SSScolor.rgb;
    // clip(normal);

    // UNOFFICIAL 2026-10-21 (Includes/LampFalloff.hlsl): att as before, the objects' decode (MatchEverywhere) or the shape.
    return LampAttOther(saturate((1 - Attenuation1) - Attenuation2)) * pointLightContribution;
}
