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

// UNOFFICIAL 2026-10-15 [Shaders.ShadowsInteriors.Forward] SmoothEdges (needs a restart): compiled in only when on
// (ShaderRecord::LoadShader), as it adds about 50 instruction slots to the 4-lamp object shaders (6 lamp slots each).
#ifndef POINT_SHADOW_SMOOTH
    #define POINT_SHADOW_SMOOTH 0
#endif

float4 NVR_PointShadowLight[6]  : register(c174); // per light of the draw, see above
float4 NVR_PointShadowParams    : register(c180); // x: strength (1 = a blocked light adds nothing), y: depth bias (share of the
                                                  // distance, 0.018 as PointShadows.fx), z: normal offset (world units per
                                                  // world unit of distance; 0 = none, as the screen-space pass), w: PCF tap
                                                  // spread (cube direction units per unit of the major axis; 0 = 1 tap)

float4 NVR_PointShadowDebug     : register(c181); // x: debug view (0 off, 1 a lamp's blocked light in that lamp's colour, 2 the lit
                                                  // area of every lamp that has a shadow tinted in its colour, 3 PointShadowTrace);
                                                  // y: NearFade (world units; 0 off), see PointShadowNear; z (view 3): which light
                                                  // of the draw the focus lamp is, -1 none; z (every other view, UNOFFICIAL
                                                  // 2026-10-20): InvisibleLightSize / sqrt 2, the lamp size of lights whose fade
                                                  // (NVR_PointShadowTint w) is negative (view 3 replaces the lighting, so the
                                                  // shadows it would size are never seen); w: Quality (PCF taps with a
                                                  // spread: 1 Medium = 4, as before; 2 High = 10; 3 Ultra = 20; Low
                                                  // arrives as spread 0 = 1 tap)
float4 NVR_PointShadowTint[6]   : register(c182); // rgb: debug colour of light k's lamp (one per cube-map slot; white = no shadow);
                                                  // w: how far light k's shadow has faded in, 0..1 (a lamp that just got one);
                                                  // negative (the same amount): an invisible light with its own lamp size
                                                  // (InvisibleLightSize, c181.z)
float4 NVR_PointShadowFocus     : register(c188); // view 3: the focus lamp (cube slot 0) as NVR_PointShadowLight, whatever the draw's
                                                  // lamps; its cube map is on NVR_PointShadowCube5
float4 NVR_PointShadowSoft      : register(c189); // UNOFFICIAL contact hardening ([Shaders.ShadowsInteriors.Forward] ContactHardening):
                                                  // x: strength (0 = off, the spread as before), y: the narrowest spread, as a
                                                  // share of the Softness spread (where a blocker touches what it shadows);
                                                  // z: LampSize / sqrt 2 (world units; 0 = the ratio above), w: the widest spread
                                                  // LampSize (or InvisibleLightSize) may give, per unit of the direction's major
                                                  // axis (MaxSoftness); 0 with contact hardening alone

samplerCUBE NVR_PointShadowCube0 : register(s8);
samplerCUBE NVR_PointShadowCube1 : register(s11);
samplerCUBE NVR_PointShadowCube2 : register(s12);
samplerCUBE NVR_PointShadowCube3 : register(s13);
samplerCUBE NVR_PointShadowCube4 : register(s14);
samplerCUBE NVR_PointShadowCube5 : register(s15);

// How much a blocker at the stored distance (distance / radius) shadows: [Shaders.ShadowsInteriors.Main] NearFade, less
// within that many world units of the lamp, nothing at the lamp itself. A real lamp is no point: a blocker that close to
// it casts a shadow too blurred to see, where a point casts it sharp, as large as the room, and on every side when the
// point is inside the blocker (a lamp at the player's head). lightW: 1 / the radius.
float4 PointShadowNear(float4 stored, float lightW) {
    return NVR_PointShadowDebug.y > 0.0f ? smoothstep(0.0f, NVR_PointShadowDebug.y * lightW, stored) : 1.0f;
}

// UNOFFICIAL Quality High/Ultra: the blocked share summed over six taps, one along each axis at spread s.
float PointShadowRing(samplerCUBE cube, float3 dir, float s, float threshold, float lightW) {
    float4 a = float4(
        texCUBElod(cube, float4(dir + float3( s, 0.0f, 0.0f), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(-s, 0.0f, 0.0f), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(0.0f,  s, 0.0f), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(0.0f, -s, 0.0f), 0.0f)).r);
    float2 b = float2(
        texCUBElod(cube, float4(dir + float3(0.0f, 0.0f,  s), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(0.0f, 0.0f, -s), 0.0f)).r);
    return dot((float4)(a <= threshold) * (float4)(a > 0.0f) * PointShadowNear(a, lightW), 1.0f) +
           dot((float2)(b <= threshold) * (float2)(b > 0.0f) * PointShadowNear(float4(b, 1.0f, 1.0f), lightW).xy, 1.0f);
}

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
    // threshold here (d < 1); 0 (a caster exactly at the light) counts as no data, as in PointShadows.fx. A blocker
    // near the lamp shadows less (PointShadowNear).
    float lit;
    [branch] if (NVR_PointShadowParams.w > 0.0f) {
        // Four taps on a rotated 2x2 grid: these four offsets give one on each diagonal of whichever face the
        // direction falls on. A face texel spans 2 * major / resolution in direction units.
        float major = max(abs(dir.x), max(abs(dir.y), abs(dir.z)));
        float s = NVR_PointShadowParams.w * major;
        // UNOFFICIAL contact hardening (PCSS-style): find the blockers' mean distance over the full spread first, then
        // narrow the spread by how far the blocker is from this point compared with the lamp: sharp where an object
        // touches the floor, the full Softness spread further out. Nothing blocked in the search: lit, the PCF skipped.
        // UNOFFICIAL 2026-10-20 LampSize (z > 0): the spread a lamp of that radius gives instead, seen from the lamp
        // LampSize x (1 / blocker distance - 1 / this point's distance), between the Softness spread and MaxSoftness's (w),
        // searched over the widest: sharp where an object touches what it shadows, softer the further the shadow falls
        // from it, and blurred out where the blocker is right at the lamp (its fixture, a fan's blades above its globes).
        // Both in one form, t = 1 / blocker - 1 / d (so (d - blocker) / blocker = d t): the spread is
        // clamp(t (z / radius x major + x s d), y s, max(s, w major)), with z = w = 0 for contact hardening alone and
        // x = 0, y = 1 for LampSize (PointShadowForward::Publish), to keep the 6-lamp shaders' instruction count down.
        // UNOFFICIAL 2026-10-20 InvisibleLightSize: an invisible light (negative fade) takes its own size, c181.z.
        [branch] if (NVR_PointShadowSoft.x + NVR_PointShadowSoft.w > 0.0f) {
            float wide = max(NVR_PointShadowSoft.w * major, s);
            float4 search = float4(
                texCUBElod(cube, float4(dir + wide * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
                texCUBElod(cube, float4(dir + wide * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
                texCUBElod(cube, float4(dir + wide * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
                texCUBElod(cube, float4(dir + wide * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
            float centre = texCUBElod(cube, float4(dir, 0.0f)).r;
            float4 isBlocker = (float4)(search <= threshold) * (float4)(search > 0.0f);
            float centreBlocker = (float)(centre <= threshold) * (float)(centre > 0.0f);
            float blockers = dot(isBlocker, 1.0f) + centreBlocker;
            [branch] if (!(blockers > 0.0f)) return 1.0f;
            float blocker = (dot(search * isBlocker, 1.0f) + centre * centreBlocker) / blockers;
            float t = 1.0f / max(blocker, 0.0001f) - 1.0f / d;
            float size = fade < 0.0f ? NVR_PointShadowDebug.z : NVR_PointShadowSoft.z;
            s = clamp(t * (size * light.w * major + NVR_PointShadowSoft.x * s * d), NVR_PointShadowSoft.y * s, wide);
        }
        float4 stored = float4(
            texCUBElod(cube, float4(dir + s * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
        // UNOFFICIAL 2026-10-15 SmoothEdges (POINT_SHADOW_SMOOTH): the cube map is read with linear filtering, so across a
        // shadow's edge the stored distance slides from the blocker's to the lit surface's over one texel. A plain compare
        // turns that into a hard step that follows the texel grid (blocky edges, worst where contact hardening narrows the
        // taps). Instead each tap is blocked by how far its distance is below the threshold, against the nearest blocker
        // among the four taps: a tap on the blocker counts fully, one on the edge partly, so the edge fades over a texel
        // as with bilinear PCF (the four taps; High and Ultra's extra rings stay plain compares, to keep the 4-lamp shaders
        // as small as possible). Off: the plain compare, exactly as before.
#if POINT_SHADOW_SMOOTH
        float invRamp = 1.0f / max(threshold - min(min(stored.x, stored.y), min(stored.z, stored.w)), 0.0001f);
        float4 blocked = saturate((threshold - stored) * invRamp) * (float4)(stored > 0.0f) * PointShadowNear(stored, light.w);
#else
        float4 blocked = (float4)(stored <= threshold) * (float4)(stored > 0.0f) * PointShadowNear(stored, light.w);
#endif
        [branch] if (NVR_PointShadowDebug.w > 1.5f) {
            // UNOFFICIAL Quality 2 (High): six more taps, one along each axis: the four across the face fill the 3x3 grid's
            // edges, the two along the face's own axis land on the centre (it counts twice). Quality 3 (Ultra): the same ten
            // again at half the spread, for a smoother edge over the same width.
            float sum = dot(blocked, 1.0f) + PointShadowRing(cube, dir, s, threshold, light.w);
            float taps = 10.0f;
            [branch] if (NVR_PointShadowDebug.w > 2.5f) {
                float h = 0.5f * s;
                float4 inner = float4(
                    texCUBElod(cube, float4(dir + h * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
                    texCUBElod(cube, float4(dir + h * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
                    texCUBElod(cube, float4(dir + h * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
                    texCUBElod(cube, float4(dir + h * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
                sum += dot((float4)(inner <= threshold) * (float4)(inner > 0.0f) * PointShadowNear(inner, light.w), 1.0f) +
                       PointShadowRing(cube, dir, h, threshold, light.w);
                taps = 20.0f;
            }
            lit = 1.0f - sum / taps;
        }
        else lit = 1.0f - dot(blocked, 0.25f);
    }
    else {
        float stored = texCUBElod(cube, float4(dir, 0.0f)).r;
        lit = 1.0f - (float)(stored <= threshold) * (float)(stored > 0.0f) * PointShadowNear(stored, light.w).x;
    }
    return lerp(1.0f, lit, NVR_PointShadowParams.x * abs(fade));
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

// UNOFFICIAL DebugView 3 (diagnostic, 2026-10-08): what the cube map of the lamp in cube slot 0 (the only red lamp in views 1
// and 2) says at this pixel, looked up from the pixel's own position, whatever lamps the game gave the object. Forward
// shadows stopped dead where one floor piece met the next while the old screen-space pass (which works from the depth
// buffer, per pixel) did not; this tells the cube map and the position apart from the per-object lamp setup.
// False colour: green = what the cube map holds in this pixel's direction, blue = the pixel's own distance (both as a share
// of the cube's reach, x 2.5), red = blocked. A surface the cube map saw itself is cyan (green = blue), an empty cube map
// bright green everywhere. A 64-unit checker anchored on the lamp (must run on across object edges); the whole object at
// 40% when the lamp is not one of the draw's lights. Dark blue: beyond the cube's reach. Orange: no lamp data reached the
// draw. Grey: the vertex shader gave no position.
float3 PointShadowTrace(float3 worldPos, float3 normal, float valid) {
    float4 light = NVR_PointShadowFocus;
    [branch] if (!(valid > 0.0f)) return 0.5f;
    [branch] if (!(light.w > 0.0f)) return float3(1.0f, 0.45f, 0.0f);
    float3 toPixel = worldPos - light.xyz;
    float3 p = toPixel + normal * (NVR_PointShadowParams.z * length(toPixel));
    float d = length(p) * light.w;
    float3 color = float3(0.0f, 0.0f, 0.15f);
    [branch] if (d < 1.0f) {
        float stored = texCUBElod(NVR_PointShadowCube5, float4(p * float3(1.0f, 1.0f, -1.0f), 0.0f)).r;
        float blocked = (float)(stored > 0.0f) * (float)(stored <= d - NVR_PointShadowParams.y * d);
        float3 cell = floor(toPixel / 64.0f);
        float checker = frac((cell.x + cell.y + cell.z) * 0.5f) > 0.25f ? 1.0f : 0.7f;
        color = float3(blocked, saturate(stored * 2.5f), saturate(d * 2.5f)) * checker;
    }
    return NVR_PointShadowDebug.z < 0.0f ? color * 0.4f : color;
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
#ifdef POINT_SHADOW_SCALAR
// UNOFFICIAL 2026-10-14 (ObjectTemplate.hlsl): float ptShadow0..5, the debug tint applied at each use (same result, fewer
// temp registers held across the lighting).
#define POINT_SHADOW_LOOKUP(k, att) PointShadow##k(ptWorldPos, ptNormal, (att) * ptValid)
#define POINT_SHADOW(k, att) PointShadowFactor(ptShadow##k, NVR_PointShadowTint[k])
#define SHADOWED(color, k, att) ((color) * POINT_SHADOW(k, att))
#else
#define POINT_SHADOW_LOOKUP(k, att) PointShadowFactor(PointShadow##k(ptWorldPos, ptNormal, (att) * ptValid), NVR_PointShadowTint[k])
#define POINT_SHADOW(k, att) ptShadow##k
#define SHADOWED(color, k, att) ((color) * ptShadow##k)
#endif

#else

#define POINT_SHADOW(k, att) 1.0f
#define SHADOWED(color, k, att) (color)

#endif
