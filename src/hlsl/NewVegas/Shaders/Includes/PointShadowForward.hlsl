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

// UNOFFICIAL 2026-10-20 POINT_SHADOW_FIT (ShaderManager::LoadShader, PointShadowForward::FitLevel): a shorter version of the
// lookups below, for a shader the GPU refuses whole. D3D9 drivers take about 3,600 instruction slots in one pixel shader; with
// HandBlood on, the six-lamp shiny-object shaders came to 3,820-4,016 indoors and were refused (P82 tester, RTX 4070 Ti SUPER;
// the GTX 1070 refuses them too): those objects then drew without lamp shadows whenever a third lamp reached them, and the
// shadows came and went. 0: as before (the first try, always). 1: High/Ultra's extra taps in a loop (the same taps and weights,
// the same shadows; about 90 slots less per lamp). 2: no High/Ultra (the DLL then caps Quality at Medium for every object, so
// they all match). 3: also no blocker search (the DLL then turns contact hardening and LampSize off for every object's lamps).
#ifndef POINT_SHADOW_FIT
    #define POINT_SHADOW_FIT 0
#endif

// UNOFFICIAL 2026-10-21 POINT_SHADOW_MAG ([Shaders.ShadowsInteriors.Forward] MagnifiedSoftness above 0 at startup; ShaderRecord::
// BuildDefines): the spread is at least MagnifiedSoftness texels where one cube texel covers several screen pixels (c188), and
// NearFade's lookup folded into the 'no data' test (PointShadowKeep: the same values, fewer instructions, the room for it). 0: as
// before, byte for byte.
#ifndef POINT_SHADOW_MAG
    #define POINT_SHADOW_MAG 0
#endif

// UNOFFICIAL 2026-10-21 POINT_SHADOW_EVEN ([Shaders.ShadowsInteriors.Forward] EvenTaps at startup): High's and Ultra's extra taps
// spread evenly (PointShadowEven: High 8 taps, Ultra 16) instead of on the 5x5 grid (10 and 20). The grid gave an edge only five tap
// positions across it, eight of Ultra's twenty on the centre line: wide (LampSize) blurs showed as stacked copies of their edge.
// 0: as before, byte for byte.
#ifndef POINT_SHADOW_EVEN
    #define POINT_SHADOW_EVEN 0
#endif

// UNOFFICIAL 2026-10-21 POINT_SHADOW_TEXEL ([Shaders.ShadowsInteriors.Forward] TexelFilter at startup): the lamp shadows filtered the
// way the sun's PCF is (Shadow.hlsl ShadowPCF3x3): plain compares at exact texel centres, 3 x 3 of them round the point, weighed with a
// quadratic B-spline by where the point sits between them (PointShadowTexel). The edge is then a smooth curve, not the texel grid's
// stair-steps. Replaces Quality's taps (Medium, High and Ultra alike) and SmoothEdges' soft compare; NearFade does not apply. 0: as before.
#ifndef POINT_SHADOW_TEXEL
    #define POINT_SHADOW_TEXEL 0
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
                                                  // lamps; its cube map is on NVR_PointShadowCube5. UNOFFICIAL 2026-10-21, every
                                                  // other view (POINT_SHADOW_MAG): MagnifiedSoftness, x y: how many screen pixels
                                                  // one cube texel covers, as a 0..1 ramp = saturate(lamp distance / camera
                                                  // distance x x + y); z: the spread it then gives at least, per unit of the
                                                  // direction's major axis (0 = off); w (POINT_SHADOW_TEXEL): the cube maps'
                                                  // face resolution (ShadowCubeMapSize)
float4 NVR_PointShadowSoft      : register(c189); // UNOFFICIAL contact hardening ([Shaders.ShadowsInteriors.Forward] ContactHardening):
                                                  // x: strength (0 = off, the spread as before), y: the narrowest spread, as a
                                                  // share of the Softness spread (where a blocker touches what it shadows);
                                                  // z: LampSize / sqrt 2 (world units; 0 = the ratio above), w: the widest spread
                                                  // LampSize (or InvisibleLightSize) may give, per unit of the direction's major
                                                  // axis (MaxSoftness); 0 with contact hardening alone

// UNOFFICIAL 2026-10-28 POINT_SHADOW_RT ([Shaders.ShadowsInteriors.RT] Enabled at startup, on NVR DXVK with the RT core: ShaderRecord::
// BuildDefines; docs/next/FORK-RT-LAMPS.md): each lamp's shadow read from a traced result instead of its cube map. The RT core's 64-bit
// helper (32-bit Windows Vulkan drivers have no ray tracing) traces up to 9 shadowed lamps per pixel from a frame's depth
// (Compute/Rt/RtLampTrace.comp; core/RtLampShadows.cpp) and NVR copies the result into NVR_RtLampVis (G32R32F): 4-bit visibility for
// lamp slots 0-5 in r, 6-8 in g's low 12 bits, a 12-bit log code of the view depth above them. The next frame's points reproject into
// that frame (NVR_RtLampReproject) and take the traced value where the depth there is their own; elsewhere (hidden in that frame, off
// its screen, a slot past MaxLamps) the cube map as before.
// The cube path stays in the shader, for the A/B (c215.x 0) and the compare view. 0: as before, byte for byte.
#ifndef POINT_SHADOW_RT
    #define POINT_SHADOW_RT 0
#endif

#if POINT_SHADOW_RT
row_major float4x4 NVR_RtLampReproject : register(c206); // camera-relative position (this frame) -> the traced frame's clip space
                                                // (w = its view depth); c206-c209 are the character shadow map's outdoors only
float4 NVR_PointShadowRT      : register(c215); // x: 0 = the cube maps (as without RT), 1 = the traced result, 2 = the compare view
                                                // (both; see PointShadowFactor); y: depth tolerance (share of the view depth); z:
                                                // log2(farZ) / 4095 (the depth code's unit); w: 1 = the darker of traced and cube map
                                                // (the RT scene has no people yet: their shadows come from the cube maps)
float4 NVR_PointShadowRTSlot[2] : register(c216); // the draw's lights 0-3 and 4-5: their cube slot (0-62) when the lamp is in the trace's table, -1 = cube map; [1].z DebugView 3's slot
sampler2D NVR_RtLampVis : register(s15);          // point / clamp, set by the DLL (PointShadowForward::Bind; no sampler_state:
                                                // the POINT variants define POINT, and raw shader compiles ignore it anyway)
                                                // on s15, the sixth lamp's cube map's: every other sampler is taken in some variant
                                                // (s9 the sun atlas, s10 the live cube). So with RT the sixth light of a draw (rare: the
                                                // six-lamp OPT passes) has no cube map: traced where it can be, else lit; DebugView 3
                                                // (which reads the focus lamp's cube on s15) shows grey
#endif

samplerCUBE NVR_PointShadowCube0 : register(s8);
samplerCUBE NVR_PointShadowCube1 : register(s11);
samplerCUBE NVR_PointShadowCube2 : register(s12);
samplerCUBE NVR_PointShadowCube3 : register(s13);
samplerCUBE NVR_PointShadowCube4 : register(s14);
#if !POINT_SHADOW_RT
samplerCUBE NVR_PointShadowCube5 : register(s15);
#endif

// How much a blocker at the stored distance (distance / radius) shadows: [Shaders.ShadowsInteriors.Main] NearFade, less
// within that many world units of the lamp, nothing at the lamp itself. A real lamp is no point: a blocker that close to
// it casts a shadow too blurred to see, where a point casts it sharp, as large as the room, and on every side when the
// point is inside the blocker (a lamp at the player's head). lightW: 1 / the radius.
float4 PointShadowNear(float4 stored, float lightW) {
    return NVR_PointShadowDebug.y > 0.0f ? smoothstep(0.0f, NVR_PointShadowDebug.y * lightW, stored) : 1.0f;
}

#if POINT_SHADOW_FIT == 1
// POINT_SHADOW_FIT 1: High's six taps (along each axis at spread s), then Ultra's ten (the four diagonals and the six axes at half
// the spread), as PointShadowCube's unrolled code takes them; w: the share of the spread.
static const float4 PointShadowTaps[16] = {
    float4( 1.0f, 0.0f, 0.0f, 1.0f), float4(-1.0f, 0.0f, 0.0f, 1.0f), float4(0.0f,  1.0f, 0.0f, 1.0f), float4(0.0f, -1.0f, 0.0f, 1.0f),
    float4(0.0f, 0.0f,  1.0f, 1.0f), float4(0.0f, 0.0f, -1.0f, 1.0f),
    float4( 1.0f,  1.0f,  1.0f, 0.5f), float4(-1.0f, -1.0f,  1.0f, 0.5f), float4( 1.0f, -1.0f, -1.0f, 0.5f), float4(-1.0f,  1.0f, -1.0f, 0.5f),
    float4( 1.0f, 0.0f, 0.0f, 0.5f), float4(-1.0f, 0.0f, 0.0f, 0.5f), float4(0.0f,  1.0f, 0.0f, 0.5f), float4(0.0f, -1.0f, 0.0f, 0.5f),
    float4(0.0f, 0.0f,  1.0f, 0.5f), float4(0.0f, 0.0f, -1.0f, 0.5f) };
#endif

#if POINT_SHADOW_EVEN
// UNOFFICIAL 2026-10-21 EvenTaps: High's four extra taps (the first four) and Ultra's eight more, as offsets in units of the spread s
// (w: 1, the share of the spread, for the POINT_SHADOW_FIT 1 loop). A 3D Latin hypercube (every axis takes every level once), so on
// each cube face the taps land on the offset's two other components at distinct places, for edges at any angle; picked by
// bench\deep-roadmap\evidence\lampedge\taps.py (random search, fewest taps bunched together over faces, edge angles and the shear
// toward a face's edges), scaled so each Quality's blur width (per-axis variance with the four Medium taps) is as before. Fewer taps
// than the grid's because scattered taps share fewer texture-cache lines: Ultra with twenty cost 0.85 ms more than the grid in the
// Lucky 38, sixteen look the same as twenty (sim7.py).
static const float4 PointShadowEven[12] = {
    float4(-0.60000f, +0.60000f, +0.20000f, 1.0f), float4(-0.20000f, -0.20000f, -0.20000f, 1.0f),
    float4(+0.20000f, -0.60000f, +0.60000f, 1.0f), float4(+0.60000f, +0.20000f, -0.60000f, 1.0f),
    float4(-0.08452f, -0.25355f, -0.59161f, 1.0f), float4(-0.42258f, +0.42258f, +0.59161f, 1.0f),
    float4(+0.59161f, -0.08452f, -0.08452f, 1.0f), float4(+0.25355f, +0.08452f, +0.42258f, 1.0f),
    float4(-0.59161f, +0.25355f, -0.42258f, 1.0f), float4(+0.42258f, -0.59161f, +0.25355f, 1.0f),
    float4(+0.08452f, +0.59161f, -0.25355f, 1.0f), float4(-0.25355f, -0.42258f, +0.08452f, 1.0f) };
#endif

#if POINT_SHADOW_MAG || POINT_SHADOW_EVEN
// UNOFFICIAL 2026-10-21: how much a tap with stored distance v counts (before the compare): 0 for v = 0 (no data), NearFade's fade
// near the lamp. POINT_SHADOW_MAG: in one, t = saturate(v x nearK), smoothstep's t x t x (3 - 2 t), nearK = 1 / (NearFade as a share
// of the radius), or 2^60 with NearFade 0 (t is then 1 for any stored distance above 0, 0 for 0): the same values as
// (v > 0) x PointShadowNear, two instructions less per four taps.
float4 PointShadowKeep(float4 v, float nearK, float lightW) {
#if POINT_SHADOW_MAG
    float4 t = saturate(v * nearK);
    return t * t * (3.0f - 2.0f * t);
#else
    return (float4)(v > 0.0f) * PointShadowNear(v, lightW);
#endif
}
#endif

#if POINT_SHADOW_EVEN
// The blocked count of four even taps from PointShadowEven[i]: plain compares, as High and Ultra's extra taps always were.
float PointShadowEven4(samplerCUBE cube, float3 dir, float s, float threshold, float nearK, float lightW, int i) {
    float4 v = float4(
        texCUBElod(cube, float4(dir + s * PointShadowEven[i].xyz, 0.0f)).r,
        texCUBElod(cube, float4(dir + s * PointShadowEven[i + 1].xyz, 0.0f)).r,
        texCUBElod(cube, float4(dir + s * PointShadowEven[i + 2].xyz, 0.0f)).r,
        texCUBElod(cube, float4(dir + s * PointShadowEven[i + 3].xyz, 0.0f)).r);
    return dot((float4)(v <= threshold) * PointShadowKeep(v, nearK, lightW), 1.0f);
}
#endif

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

#if POINT_SHADOW_MAG
// PointShadowRing with PointShadowKeep (the same values).
float PointShadowRingK(samplerCUBE cube, float3 dir, float s, float threshold, float nearK) {
    float4 a = float4(
        texCUBElod(cube, float4(dir + float3( s, 0.0f, 0.0f), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(-s, 0.0f, 0.0f), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(0.0f,  s, 0.0f), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(0.0f, -s, 0.0f), 0.0f)).r);
    float4 b = float4(
        texCUBElod(cube, float4(dir + float3(0.0f, 0.0f,  s), 0.0f)).r,
        texCUBElod(cube, float4(dir + float3(0.0f, 0.0f, -s), 0.0f)).r, 1.0f, 1.0f);
    return dot((float4)(a <= threshold) * PointShadowKeep(a, nearK, 0.0f), 1.0f) + dot(((float4)(b <= threshold) * PointShadowKeep(b, nearK, 0.0f)).xy, 1.0f);
}
#endif

#if POINT_SHADOW_TEXEL
// UNOFFICIAL 2026-10-21 TexelFilter: one lattice level of PointShadowTexel. q: the point in the face's texel grid (texel centres at whole
// numbers); k: the lattice spacing in texels (1, 2, 4 ...: lattice points at multiples of k, on texel centres); M, A, B (w 0): the
// major axis (the direction's own component) and the face's two axes x the major component, so M + u A + v B points at the face
// point (u, v); scale: 2 / resolution. The blocked share of the 3 x 3 lattice points round q, quadratic B-spline weights by where q
// sits between them (as the sun's Shadow.hlsl ShadowPCF3x3). A stored 0 (no data) is lit, as everywhere; NearFade is not applied
// here (it would cost the six-lamp shaders too many instructions; 0 by default).
float PointShadowTexelLevel(samplerCUBE cube, float2 q, float k, float4 M, float4 A, float4 B, float scale, float threshold) {
    float2 g = q / k;
    float2 c = floor(g + 0.5f);                     // the nearest lattice point
    float2 f = g - c;                               // -0.5 .. 0.5 from it
    float3 wx = float3(0.5f * (0.5f - f.x) * (0.5f - f.x), 0.75f - f.x * f.x, 0.5f * (0.5f + f.x) * (0.5f + f.x));
    float3 wy = float3(0.5f * (0.5f - f.y) * (0.5f - f.y), 0.75f - f.y * f.y, 0.5f * (0.5f + f.y) * (0.5f + f.y));
    float3 ux = ((c.x + float3(-1.0f, 0.0f, 1.0f)) * k + 0.5f) * scale;  // the lattice columns' face coordinates + 1, x the major component
    float3 vy = ((c.y + float3(-1.0f, 0.0f, 1.0f)) * k + 0.5f) * scale;  // and rows'
    float3 rows;
    [unroll] for (int y = 0; y < 3; y++) {
        float4 row = M + vy[y] * B;
        float3 v = float3(texCUBElod(cube, row + ux.x * A).r, texCUBElod(cube, row + ux.y * A).r, texCUBElod(cube, row + ux.z * A).r);
        rows[y] = dot((float3)(v <= threshold) * (float3)(v > 0.0f), wx);
    }
    return dot(rows, wy);
}

// UNOFFICIAL 2026-10-21 TexelFilter: the blocked share at spread s (direction units, as the taps' spread), filtered as the sun's PCF:
// plain compares at texel centres of a 3 x 3 lattice round the point, weighed with a quadratic B-spline, so the edge is a smooth curve
// instead of the texel grid's stair-steps. The lattice spacing is 1.22 x the spread in texels (the same blur width as Ultra's taps at
// that spread: the quadratic B-spline's sigma is half the spacing), at least one texel (the sun's own filter). A wider blur takes the
// coarser lattices 2, 4, 8 ... texels apart, blended between the two nearest by the fraction, so the blur widens smoothly where
// LampSize or contact hardening change the spread from pixel to pixel; the second lattice is read only there (and not at all in the
// shorter POINT_SHADOW_FIT versions, for a shader the GPU refused whole: there the nearest lattice alone). Taps past a face's edge
// read the next face (not on its texel centres: a little less smooth along the cube's seams only).
float PointShadowTexel(samplerCUBE cube, float3 dir, float major, float s, float threshold, float lightW) {
    float3 ad = abs(dir);
    float xm = (float)(ad.x >= max(ad.y, ad.z));
    float zm = (1.0f - xm) * (float)(ad.z > ad.y);
    float4 A = float4(1.0f - xm, xm, 0.0f, 0.0f) * major;       // the face's first axis: y on the X faces, x on the others
    float4 B = float4(0.0f, zm, 1.0f - zm, 0.0f) * major;       // and its second: y on the Z faces, z on the others
    float4 M = float4(dir * float3(xm, 1.0f - xm - zm, zm), 0.0f) - A - B;   // the major axis, less the face's (-1, -1) corner
    float resolution = NVR_PointShadowFocus.w;
    float2 q = (float2(dot(dir, A.xyz), dot(dir, B.xyz)) / (major * major) + 1.0f) * (0.5f * resolution) - 0.5f;   // texel centres at whole numbers
    float spacing = max(1.0f, 0.61f * s * resolution / major);  // 1.22 x the spread in texels (s / major x resolution / 2)
    float k = exp2(floor(log2(spacing)));
    float mix = spacing / k - 1.0f;
    float scale = 2.0f / resolution;                            // texels -> face coordinates (A and B carry the major component)
    float blocked = PointShadowTexelLevel(cube, q, k, M, A, B, scale, threshold);
#if POINT_SHADOW_FIT < 1
    [branch] if (mix > 0.001f) blocked = lerp(blocked, PointShadowTexelLevel(cube, q, 2.0f * k, M, A, B, scale, threshold), mix);
#endif
    return blocked;
}
#endif

#if POINT_SHADOW_RT
// UNOFFICIAL 2026-10-28 RT lamp shadows: how much of the lamp in cube slot `slot` (0-62) reaches this point (1 lit, 0 blocked), or
// negative when the traced frame did not see this point: -1 off its screen or behind its camera, -2 - error (up to -3) another depth
// there (the sky, something in front). The caller then takes the cube map.
// Point sampled: the channels hold packed integers (exact below 2^24; dividing by powers of 2 keeps them exact).
float PointShadowTraced(float slot, float3 worldPos) {
    float4 clip = mul(float4(worldPos, 1.0f), NVR_RtLampReproject);
    [branch] if (!(clip.w > 1.0f)) return -1.0f;
    float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
    [branch] if (any(abs(uv - 0.5f) > 0.5f)) return -1.0f;
    float2 traced = tex2Dlod(NVR_RtLampVis, float4(uv, 0.0f, 0.0f)).xy;
    float depthCode = floor(traced.y / 1024.0f);
    float depthError = abs(exp2(depthCode * NVR_PointShadowRT.z) - clip.w) / clip.w;
    [branch] if (!(depthError <= NVR_PointShadowRT.y)) return -2.0f - saturate(depthError);  // -2..-3: the depth did not match
    // 2026-10-29 v4 (RtLampTrace.comp): three entries, cube slot + 64 x visibility (16 steps); the lamp's own slot, or -4: this pixel
    // traced other lamps (they bring it more light), the cube map then
    float hi = floor(traced.x / 1024.0f);
    float3 entries = float3(traced.x - 1024.0f * hi, hi, traced.y - 1024.0f * depthCode);
    float3 visible = floor(entries / 64.0f);
    float3 slots = entries - 64.0f * visible;
    float3 match = (float3)(abs(slots - slot) < 0.5f);
    [branch] if (!(dot(match, 1.0f) > 0.5f)) return -4.0f;
    return dot(match, visible) / (15.0f * dot(match, 1.0f));
}

// UNOFFICIAL 2026-10-29 RT coverage view (NVR_PointShadowRT.x 3, [Shaders.ShadowsInteriors.RT] DebugView 2): the colour the object and
// skin shaders write INSTEAD of their lighting (ObjectTemplate.hlsl, SkinTemplate.hlsl), for the first of the draw's lights that has a
// traced slot: GREEN = its traced value is used (brightness = how lit), BLUE = off the traced frame's screen or behind its camera,
// YELLOW = this pixel traced other lamps (stronger ones there),
// RED = the traced frame saw another depth there (brighter = further off), GREY = no light of this draw is traced. Exact per pixel
// (an earlier version sent the classes through the lighting as encoded shadow values, which other lighting maths then distorted).
float3 PointShadowRtCoverage(float3 worldPos) {
    // UNOFFICIAL 2026-10-29 DebugView 3 (x 4): ONE traced lamp, NVR_PointShadowRTSlot[1].z, at every pixel whatever the draw's own lamps:
    // white = lit .. black = blocked, MAGENTA = no traced data there (off the traced frame, another depth).
    [branch] if (NVR_PointShadowRT.x > 3.5f) {
        float one = PointShadowTraced(NVR_PointShadowRTSlot[1].z, worldPos);
        return one >= 0.0f ? one : float3(1.0f, 0.0f, 1.0f);
    }
    float slot = NVR_PointShadowRTSlot[0].x;
    slot = slot >= 0.0f ? slot : NVR_PointShadowRTSlot[0].y;
    slot = slot >= 0.0f ? slot : NVR_PointShadowRTSlot[0].z;
    slot = slot >= 0.0f ? slot : NVR_PointShadowRTSlot[0].w;
    slot = slot >= 0.0f ? slot : NVR_PointShadowRTSlot[1].x;
    slot = slot >= 0.0f ? slot : NVR_PointShadowRTSlot[1].y;
    [branch] if (slot < 0.0f) return 0.25f;
    float traced = PointShadowTraced(slot, worldPos);
    return traced >= 0.0f ? float3(0.0f, 0.2f + 0.8f * traced, 0.0f) : traced > -1.5f ? float3(0.0f, 0.0f, 1.0f) :
           traced < -3.5f ? float3(0.8f, 0.8f, 0.0f) : float3(0.3f + 0.7f * (-2.0f - traced), 0.0f, 0.0f);   // yellow: not traced here
}

// The compare view (NVR_PointShadowRT.x 2): both answers packed into the factor PointShadowFactor decodes, the traced and the cube
// map's lit share in 16 steps each (where nothing was traced, the cube map's for both).
float PointShadowDone(float cubeLit, float rayLit, float fade) {
    [branch] if (NVR_PointShadowRT.x > 1.5f)
        return 2.0f + round(saturate(rayLit < 0.0f ? cubeLit : rayLit) * 15.0f) + 16.0f * round(saturate(cubeLit) * 15.0f);
    // c215.w: people's shadows from the cube maps (the darker of the two where something was traced)
    return lerp(1.0f, rayLit >= 0.0f ? min(cubeLit, rayLit) : cubeLit, NVR_PointShadowParams.x * abs(fade));
}
#define POINT_SHADOW_EARLY_LIT PointShadowDone(1.0f, rayLit, fade)
#define POINT_SHADOW_RT_PARAM , float rtSlot
#define POINT_SHADOW_RT_ARG(bit) , bit
#define POINT_SHADOW_DONE(lit) PointShadowDone(lit, rayLit, fade)
#else
#define POINT_SHADOW_EARLY_LIT 1.0f
#define POINT_SHADOW_RT_PARAM
#define POINT_SHADOW_RT_ARG(bit)
#define POINT_SHADOW_DONE(lit) lerp(1.0f, lit, NVR_PointShadowParams.x * abs(fade))
#endif

// 1 lit, 0 fully shadowed (before strength). worldPos: camera-relative (GetShadowWorldPos in the VS). normal: the
// camera-facing geometric normal where the shader has one (GetShadowGeometricNormal), else 0. att: this light's
// attenuation here; 0 (out of reach, or no valid world position) skips the lookup. texCUBElod only, so it is legal
// inside the dynamic branches.
float PointShadowCube(samplerCUBE cube, float4 light, float fade, float3 worldPos, float3 normal, float att POINT_SHADOW_RT_PARAM) {
    // First the test on the constant alone (the same for the whole draw, so nearly free): a light without a cube map,
    // every light outdoors and every light while the feature is off end here.
    [branch] if (!(light.w > 0.0f)) return 1.0f;
    float3 toPixel = worldPos - light.xyz;
    float len = length(toPixel);
    [branch] if (!(att > 0.0f) || !(len * light.w < 1.0f)) return 1.0f;   // beyond the cube map: nothing recorded
#if POINT_SHADOW_RT
    // RT lamp shadows: the traced result instead of the cube map (x 1), or both for the compare view (x 2). Same reach as the cube
    // map. Where the traced frame did not see this point, the cube map (and in the compare view both agree there: it shows the traced
    // area only).
    float rayLit = -1.0f;
    [branch] if (NVR_PointShadowRT.x > 0.0f && rtSlot >= 0.0f) {
        rayLit = PointShadowTraced(rtSlot, worldPos);
        [branch] if (NVR_PointShadowRT.x < 1.5f && rayLit >= 0.0f && NVR_PointShadowRT.w < 0.5f) return lerp(1.0f, rayLit, NVR_PointShadowParams.x * abs(fade));
    }
#endif

    // Normal offset: a cube texel's world size grows with the distance, so does the push off the surface.
    float3 p = toPixel + normal * (NVR_PointShadowParams.z * len);
    float d = length(p) * light.w;                       // what the cube map stores: distance / radius
    float threshold = d - NVR_PointShadowParams.y * d;    // lit where stored + bias * d > d (Effects\Includes\Shadows.hlsl)
#if POINT_SHADOW_MAG
    float nearK = NVR_PointShadowDebug.y > 0.0f ? 1.0f / (NVR_PointShadowDebug.y * light.w) : 1152921504606846976.0f;	// PointShadowKeep
#elif POINT_SHADOW_EVEN
    float nearK = 0.0f;	// (unused: PointShadowKeep takes NearFade from light.w)
#endif

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
#if POINT_SHADOW_MAG
        // UNOFFICIAL 2026-10-21 MagnifiedSoftness (c188, PointShadowForward::Publish; z 0 = off, s exactly as before): a cube texel is
        // (2 / resolution) x the lamp's distance wide, a screen pixel (its angle) x the camera's distance, so far from its lamp and
        // close to the camera one texel covers many pixels and a narrow edge shows its stair-steps. There the spread is at least
        // MagnifiedSoftness texels, ramping in between 2 and 8 pixels per texel; the blocker search and LampSize take it as their
        // narrowest spread.
        s = max(s, saturate(len * rsqrt(dot(worldPos, worldPos)) * NVR_PointShadowFocus.x + NVR_PointShadowFocus.y) * NVR_PointShadowFocus.z * major);
#endif
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
#if POINT_SHADOW_FIT < 3
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
            [branch] if (!(blockers > 0.0f)) return POINT_SHADOW_EARLY_LIT;
            float blocker = (dot(search * isBlocker, 1.0f) + centre * centreBlocker) / blockers;
            float t = 1.0f / max(blocker, 0.0001f) - 1.0f / d;
            float size = fade < 0.0f ? NVR_PointShadowDebug.z : NVR_PointShadowSoft.z;
            s = clamp(t * (size * light.w * major + NVR_PointShadowSoft.x * s * d), NVR_PointShadowSoft.y * s, wide);
        }
#endif
#if POINT_SHADOW_TEXEL
        // UNOFFICIAL 2026-10-21 TexelFilter: the sun-style filter at this spread (PointShadowTexel) instead of Quality's taps.
        lit = 1.0f - PointShadowTexel(cube, dir, major, s, threshold, light.w);
#else
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
#if POINT_SHADOW_SMOOTH && POINT_SHADOW_MAG
        float invRamp = 1.0f / max(threshold - min(min(stored.x, stored.y), min(stored.z, stored.w)), 0.0001f);
        float4 blocked = saturate((threshold - stored) * invRamp) * PointShadowKeep(stored, nearK, light.w);
#elif POINT_SHADOW_SMOOTH
        float invRamp = 1.0f / max(threshold - min(min(stored.x, stored.y), min(stored.z, stored.w)), 0.0001f);
        float4 blocked = saturate((threshold - stored) * invRamp) * (float4)(stored > 0.0f) * PointShadowNear(stored, light.w);
#elif POINT_SHADOW_MAG
        float4 blocked = (float4)(stored <= threshold) * PointShadowKeep(stored, nearK, light.w);
#else
        float4 blocked = (float4)(stored <= threshold) * (float4)(stored > 0.0f) * PointShadowNear(stored, light.w);
#endif
#if POINT_SHADOW_FIT < 2
        [branch] if (NVR_PointShadowDebug.w > 1.5f) {
            // UNOFFICIAL Quality 2 (High): six more taps, one along each axis: the four across the face fill the 3x3 grid's
            // edges, the two along the face's own axis land on the centre (it counts twice). Quality 3 (Ultra): the same ten
            // again at half the spread, for a smoother edge over the same width.
#if POINT_SHADOW_FIT == 1
            // (POINT_SHADOW_FIT 1: one tap a turn from PointShadowTaps; the sum in another order, the same within rounding.)
#if POINT_SHADOW_EVEN
            // EvenTaps: High's four even taps, Ultra's twelve (PointShadowEven).
            const int count = NVR_PointShadowDebug.w > 2.5f ? 12 : 4;
            float extra = 0.0f;
            [loop] for (int i = 0; i < 12; i++) {
                [branch] if (i >= count) break;
                const float4 tap = PointShadowEven[i];
#else
            const int count = NVR_PointShadowDebug.w > 2.5f ? 16 : 6;
            float extra = 0.0f;
            [loop] for (int i = 0; i < 16; i++) {
                [branch] if (i >= count) break;
                const float4 tap = PointShadowTaps[i];
#endif
                const float v = texCUBElod(cube, float4(dir + (s * tap.w) * tap.xyz, 0.0f)).r;
#if POINT_SHADOW_MAG || POINT_SHADOW_EVEN
                extra += (float)(v <= threshold) * PointShadowKeep(v.xxxx, nearK, light.w).x;
#else
                extra += (float)(v <= threshold) * (float)(v > 0.0f) * PointShadowNear(v.xxxx, light.w).x;
#endif
            }
            lit = 1.0f - (dot(blocked, 1.0f) + extra) / (4.0f + (float)count);
#elif POINT_SHADOW_EVEN
            // EvenTaps: High = the four Medium taps and PointShadowEven's first four, Ultra all twelve (equal weights).
            float sum = dot(blocked, 1.0f) + PointShadowEven4(cube, dir, s, threshold, nearK, light.w, 0);
            float taps = 8.0f;
            [branch] if (NVR_PointShadowDebug.w > 2.5f) {
                sum += PointShadowEven4(cube, dir, s, threshold, nearK, light.w, 4) + PointShadowEven4(cube, dir, s, threshold, nearK, light.w, 8);
                taps = 16.0f;
            }
            lit = 1.0f - sum / taps;
#elif POINT_SHADOW_MAG
            float sum = dot(blocked, 1.0f) + PointShadowRingK(cube, dir, s, threshold, nearK);
            float taps = 10.0f;
            [branch] if (NVR_PointShadowDebug.w > 2.5f) {
                float h = 0.5f * s;
                float4 inner = float4(
                    texCUBElod(cube, float4(dir + h * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
                    texCUBElod(cube, float4(dir + h * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
                    texCUBElod(cube, float4(dir + h * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
                    texCUBElod(cube, float4(dir + h * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
                sum += dot((float4)(inner <= threshold) * PointShadowKeep(inner, nearK, light.w), 1.0f) +
                       PointShadowRingK(cube, dir, h, threshold, nearK);
                taps = 20.0f;
            }
            lit = 1.0f - sum / taps;
#else
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
#endif
        }
        else lit = 1.0f - dot(blocked, 0.25f);
#else
        lit = 1.0f - dot(blocked, 0.25f);
#endif
#endif
    }
    else {
        float stored = texCUBElod(cube, float4(dir, 0.0f)).r;
#if POINT_SHADOW_MAG
        lit = 1.0f - (float)(stored <= threshold) * PointShadowKeep(stored.xxxx, nearK, light.w).x;
#else
        lit = 1.0f - (float)(stored <= threshold) * (float)(stored > 0.0f) * PointShadowNear(stored, light.w).x;
#endif
    }
    return POINT_SHADOW_DONE(lit);
}

// What light k's colour is multiplied by: its shadow, or in the debug views (NVR_PointShadowDebug.x, the same for the
// whole draw, so one cheap branch) 1: blocked light drawn in the lamp's colour instead of removed, 2: the lamp's lit
// area tinted in its colour. Lamps without a shadow have white, so the views leave them as they are.
float3 PointShadowFactor(float shadow, float4 tint) {
#if POINT_SHADOW_RT
    // RT compare view (PointShadowDone): white where both are lit, black where both are blocked, RED where only the traced result
    // has a blocker, GREEN where only the cube map does (light the rays let through: a leak, or a cube-map artifact the rays fix).
    [branch] if (shadow > 1.5f) {
        float e = shadow - 2.0f;
        float cubeSteps = floor(e / 16.0f);
        float ray = (e - 16.0f * cubeSteps) / 15.0f;
        float cubeLit = cubeSteps / 15.0f;
        float both = min(ray, cubeLit);
        return float3(both + max(cubeLit - ray, 0.0f), both + max(ray - cubeLit, 0.0f), both);
    }
#endif
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
#if POINT_SHADOW_RT
    return 0.5f;    // (RT builds: the focus lamp's cube map has no sampler, see NVR_RtLampVis)
#else
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
#endif
}

// One function per light index of the draw: ps_3_0 cannot index samplers at run time.
float PointShadow0(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube0, NVR_PointShadowLight[0], NVR_PointShadowTint[0].w, worldPos, normal, att POINT_SHADOW_RT_ARG(NVR_PointShadowRTSlot[0].x)); }
float PointShadow1(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube1, NVR_PointShadowLight[1], NVR_PointShadowTint[1].w, worldPos, normal, att POINT_SHADOW_RT_ARG(NVR_PointShadowRTSlot[0].y)); }
float PointShadow2(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube2, NVR_PointShadowLight[2], NVR_PointShadowTint[2].w, worldPos, normal, att POINT_SHADOW_RT_ARG(NVR_PointShadowRTSlot[0].z)); }
float PointShadow3(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube3, NVR_PointShadowLight[3], NVR_PointShadowTint[3].w, worldPos, normal, att POINT_SHADOW_RT_ARG(NVR_PointShadowRTSlot[0].w)); }
float PointShadow4(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube4, NVR_PointShadowLight[4], NVR_PointShadowTint[4].w, worldPos, normal, att POINT_SHADOW_RT_ARG(NVR_PointShadowRTSlot[1].x)); }
#if POINT_SHADOW_RT
// RT builds: the sixth light has no cube map (NVR_RtLampVis took s15): the traced result where there is one, else lit.
float PointShadow5(float3 worldPos, float3 normal, float att) {
    float4 light = NVR_PointShadowLight[5];
    [branch] if (!(light.w > 0.0f) || !(att > 0.0f) || !(NVR_PointShadowRT.x > 0.0f) || NVR_PointShadowRTSlot[1].y < 0.0f) return 1.0f;
    [branch] if (!(length(worldPos - light.xyz) * light.w < 1.0f)) return 1.0f;
    float traced = PointShadowTraced(NVR_PointShadowRTSlot[1].y, worldPos);
    return traced < 0.0f ? 1.0f : lerp(1.0f, traced, NVR_PointShadowParams.x * abs(NVR_PointShadowTint[5].w));
}
#else
float PointShadow5(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube5, NVR_PointShadowLight[5], NVR_PointShadowTint[5].w, worldPos, normal, att POINT_SHADOW_RT_ARG(NVR_PointShadowRTSlot[1].y)); }
#endif

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
