// UNOFFICIAL forward point-light shadows (interiors) -- PROTOTYPE, see NVR-Lunacy\docs\interior-forward-shadows.md.
//
// The screen-space interior pass (Effects\PointShadows.fx + ShadowsInteriors.fx) can only darken the finished image:
// one mask for all lights, ambient and glow included. Here each point light's own direct term is multiplied by its own
// cube-map shadow inside the object shader, the way GetSunShadow (Shadow.hlsl) treats the sun outdoors.
//
// Compiled in only when INTERIOR_SHADOWS is 1 (ShaderRecord::LoadShader, from the setting at game start), so with the
// setting off the object shaders are exactly what they were. Whether it RUNS is decided per draw by the DLL.
//
// Per draw (SetShadersHook, NewVegas\Hooks\Render.cpp), the DLL walks the game's render pass light list
// (BSShaderManager::pCurrentRenderPass, 0x011F91E0: +0x09 light count, +0x0C ShadowSceneLight* array; its order is
// the order of PSLightColor) and, for each light k that NVR rendered a shadow cube map for, writes:
//   NVR_PointShadowLight[k] = xyz: the light's position as the cube map was rendered, minus the camera position
//                             (the camera-relative space of GetShadowWorldPos); w: 1 / the radius the cube map
//                             stores distances over (0 = no shadow for this light, the default),
// and binds that cube map to NVR_PointShadowCube<k>. Not TESR_ names on purpose: ShaderRecord::SetCT binds TESR_
// constants by name when the shader changes, and these change per draw.
//
// Registers: c174-c187 sit above GunFX's c171-c173 and below every ps_3_0 limit; s8 and s11-s15 are free in every
// pixel shader that includes this (ObjectTemplate, SKIN2002/2006, SkinVPSTemplate, SM3002/3003/3005/3007: game textures
// use s0-s7, the sun atlas s9, its cross-fade copy s10).

#ifndef INTERIOR_SHADOWS
    #define INTERIOR_SHADOWS 0
#endif

#if INTERIOR_SHADOWS

float4 NVR_PointShadowLight[6]  : register(c174); // per light of the draw, see above
float4 NVR_PointShadowParams    : register(c180); // x: strength (1 = a blocked light adds nothing), y: depth bias (share of the
                                                  // distance, 0.018 as PointShadows.fx), z: normal offset (world units per
                                                  // world unit of distance; 0 = none, as the screen-space pass), w: PCF tap
                                                  // spread (cube direction units per unit of the major axis; 0 = 1 tap)

float4 NVR_PointShadowDebug     : register(c181); // x: debug view (0 off, 1 a lamp's blocked light in that lamp's colour, 2 the lit
                                                  // area of every lamp that has a shadow tinted in its colour)
float4 NVR_PointShadowTint[6]   : register(c182); // rgb: debug colour of light k's lamp (one per cube-map slot; white = no shadow);
                                                  // w: how far light k's shadow has faded in, 0..1 (a lamp that just got one)

samplerCUBE NVR_PointShadowCube0 : register(s8);
samplerCUBE NVR_PointShadowCube1 : register(s11);
samplerCUBE NVR_PointShadowCube2 : register(s12);
samplerCUBE NVR_PointShadowCube3 : register(s13);
samplerCUBE NVR_PointShadowCube4 : register(s14);
samplerCUBE NVR_PointShadowCube5 : register(s15);

// 1 lit, 0 fully shadowed (before strength). worldPos: camera-relative (GetShadowWorldPos in the VS). normal: the
// camera-facing geometric normal where the shader has one (GetShadowGeometricNormal), else 0. att: this light's
// attenuation here; 0 (out of reach, or no valid world position) skips the lookup. texCUBElod only, so it is legal
// inside the dynamic branches.
float PointShadowCube(samplerCUBE cube, float4 light, float fade, float3 worldPos, float3 normal, float att) {
    // First the test on the constant alone (the same for the whole draw, so nearly free): a light without a cube map,
    // every light outdoors and every light while the feature is off end here.
    [branch] if (!(light.w > 0.0f)) return 1.0f;
    float3 toPixel = worldPos - light.xyz;
    float len = length(toPixel);
    [branch] if (!(att > 0.0f) || !(len * light.w < 1.0f)) return 1.0f;   // beyond the cube map: nothing recorded

    // Normal offset: a cube texel's world size grows with the distance, so does the push off the surface.
    float3 p = toPixel + normal * (NVR_PointShadowParams.z * len);
    float d = length(p) * light.w;                       // what the cube map stores: distance / radius
    float threshold = d - NVR_PointShadowParams.y * d;    // lit where stored + bias * d > d (Effects\Includes\Shadows.hlsl)

    // Face convention of ShadowCubeMap.vso: lookup = (light - point) * (-1, -1, 1) = (point - light) * (1, 1, -1).
    float3 dir = p * float3(1.0f, 1.0f, -1.0f);

    // Lit where something nearer than the point was not drawn. A texel nothing was drawn into holds 1, above any
    // threshold here (d < 1); 0 (a caster exactly at the light) counts as no data, as in PointShadows.fx.
    float lit;
    [branch] if (NVR_PointShadowParams.w > 0.0f) {
        // Four taps on a rotated 2x2 grid: these four offsets give one on each diagonal of whichever face the
        // direction falls on. A face texel spans 2 * major / resolution in direction units.
        float s = NVR_PointShadowParams.w * max(abs(dir.x), max(abs(dir.y), abs(dir.z)));
        float4 stored = float4(
            texCUBElod(cube, float4(dir + s * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
        lit = dot(saturate((float4)(stored > threshold) + (float4)(stored <= 0.0f)), 0.25f);
    }
    else {
        float stored = texCUBElod(cube, float4(dir, 0.0f)).r;
        lit = saturate((float)(stored > threshold) + (float)(stored <= 0.0f));
    }
    return lerp(1.0f, lit, NVR_PointShadowParams.x * fade);
}

// What light k's colour is multiplied by: its shadow, or in the debug views (NVR_PointShadowDebug.x, the same for the
// whole draw, so one cheap branch) 1: blocked light drawn in the lamp's colour instead of removed, 2: the lamp's lit
// area tinted in its colour. Lamps without a shadow have white, so the views leave them as they are.
float3 PointShadowFactor(float shadow, float4 tint) {
    float3 factor = shadow;
    [branch] if (NVR_PointShadowDebug.x > 0.0f) {
        factor = NVR_PointShadowDebug.x < 1.5f ? shadow + (1.0f - shadow) * tint.rgb
                                               : shadow * lerp(1.0f, tint.rgb, 0.6f);
    }
    return factor;
}

// One function per light index of the draw: ps_3_0 cannot index samplers at run time.
float PointShadow0(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube0, NVR_PointShadowLight[0], NVR_PointShadowTint[0].w, worldPos, normal, att); }
float PointShadow1(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube1, NVR_PointShadowLight[1], NVR_PointShadowTint[1].w, worldPos, normal, att); }
float PointShadow2(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube2, NVR_PointShadowLight[2], NVR_PointShadowTint[2].w, worldPos, normal, att); }
float PointShadow3(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube3, NVR_PointShadowLight[3], NVR_PointShadowTint[3].w, worldPos, normal, att); }
float PointShadow4(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube4, NVR_PointShadowLight[4], NVR_PointShadowTint[4].w, worldPos, normal, att); }
float PointShadow5(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube5, NVR_PointShadowLight[5], NVR_PointShadowTint[5].w, worldPos, normal, att); }

// The object shader looks every light's shadow up once, inside one branch on NVR_PointShadowParams.x (0 outdoors and
// whenever the feature is idle, so a draw without shadows pays for that one skipped branch), into float3 ptShadow0..5
// (float3 for the debug views; the plain shadow in all three channels otherwise):
//   POINT_SHADOW_LOOKUP(k, att): the lookup; ptWorldPos, ptNormal and ptValid (1 when an NVR vertex shader wrote the
//                                world position, 0 under a vanilla one) must be in scope -- see ObjectTemplate.hlsl.
//   POINT_SHADOW(k, att), SHADOWED(colour, k, att): light k's shadow, light k's colour with it (att is only there to
//                                keep the call sites readable; the lookup already used it).
#define POINT_SHADOW_LOOKUP(k, att) PointShadowFactor(PointShadow##k(ptWorldPos, ptNormal, (att) * ptValid), NVR_PointShadowTint[k])
#define POINT_SHADOW(k, att) ptShadow##k
#define SHADOWED(color, k, att) ((color) * ptShadow##k)

#else

#define POINT_SHADOW(k, att) 1.0f
#define SHADOWED(color, k, att) (color)

#endif
