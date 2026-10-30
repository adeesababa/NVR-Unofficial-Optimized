// UNOFFICIAL 2026-10-28 CapturedNormals (core/CapturedNormals.h, docs/next/CAPTURED-NORMALS.md; tester macintroll's proposal): while the
// world's main opaque list draws, NVR's lit templates (ObjectTemplate, ParallaxTemplate, SkinTemplate, TerrainTemplate) write the surface's
// smooth interpolated vertex normal (world space, after skinning, BEFORE the normal map) to a fourth render target, COLOR3:
//   rg  the normal, octahedral (Includes/Shadow.hlsl ShadowEncodeNormal), facing the camera
//   b   the camera distance (length of the camera-relative world position), which readers compare against the depth buffer
//   a   1 = written by a template, 2 = by an actor's (bone-skinned draws: SKIN, or the skin template; 2026-10-28: snow keeps off actors)
//       (0 = cleared, or a variant with no smooth normal)
// The target is bound only inside that list and only for those templates' opaque draws (anything else masks it off, COLORWRITEENABLE3);
// anywhere else COLOR3 is discarded. Compiled in only with [Shaders.SnowAccumulation.Main] CapturedNormals on at startup
// (CAPTURED_NORMALS_ON, ShaderRecord::BuildDefines): off, the templates are exactly as before. Needs Includes/Shadow.hlsl first.
#ifndef NVR_CAPTURED_NORMALS_HLSL
#define NVR_CAPTURED_NORMALS_HLSL

#ifndef CAPTURED_NORMALS_ON
    #define CAPTURED_NORMALS_ON 0
#endif

#if CAPTURED_NORMALS_ON
    #define CAPTURED_NORMAL_INVALID float4(0.0f, 0.0f, 0.0f, 0.0f)
    // n: the smooth vertex normal (camera-relative world space, any length), worldPos: the camera-relative world position.
    float4 CapturedNormalOut(float3 n, float3 worldPos) {
        const float len2 = dot(n, n);
        [flatten] if (len2 < 1.0e-6f) return CAPTURED_NORMAL_INVALID;
        n *= rsqrt(len2);
        n *= dot(n, worldPos) > 0.0f ? -1.0f : 1.0f;	// the side the camera sees (two-sided cards, cloth)
        #if defined(SKIN) || defined(CAPTURED_ACTOR)
            return float4(ShadowEncodeNormal(n), length(worldPos), 2.0f);
        #else
            return float4(ShadowEncodeNormal(n), length(worldPos), 1.0f);
        #endif
    }

    // UNOFFICIAL 2026-10-29 MaterialBuffer (core/CapturedNormals.h; [Main.Main.Quality] MaterialBuffer, restart; CAPTURED_NORMALS_ON 2): the
    // templates' spare COLOR2 = TESR_MaterialBuffer while the main list draws: r the material class (Render.cpp per draw: 1 object, 2 worn
    // gear (skinned), 3 cloth, 4 land, 5 plants), g the drawn object's world bound radius (thickness, an upper bound), b the roughness the
    // shader lit with (-1: none), a 1. Actors' skin is not in it (its template's COLOR2 is the skin blur's): the normals' a == 2 marks it.
    #if CAPTURED_NORMALS_ON == 2
        float4 NVR_MaterialDraw : register(c217);	// x class, y bound radius (free in the lit templates: only ParticleLight.hlsl's lamp array covers c215-c226)
        #define CapturedMaterialOut(roughness) float4(NVR_MaterialDraw.x, NVR_MaterialDraw.y, (roughness), 1.0f)
        #define CapturedMaterialOutClass(materialClass, roughness) float4((materialClass), NVR_MaterialDraw.y, (roughness), 1.0f)
    #else
        #define CapturedMaterialOut(roughness) 0.0f
        #define CapturedMaterialOutClass(materialClass, roughness) 0.0f
    #endif
#endif

#endif
