// UNOFFICIAL 2026-10-28 (NVR DXVK, RT reflections, docs/next/FORK-RT-REFLECTIONS.md): what the two in-game RT passes of Reflections share
// (ReflectionsRayGen.frag, ReflectionsRTResolve.frag). The rays themselves are traced OUTSIDE the game: AMD's 32-bit Vulkan driver has
// no ray tracing, so the RT core (session rt-sun-core, docs/next/FORK-RT-CORE.md) runs them in its 64-bit helper (NvrRtHelper.exe)
// with RT/ReflectionsRT.comp. Per frame:
//   1 RayGen (here, half resolution): the screen march, as ReflectionsTrace; writes what it found (rgb linear, a confidence), the
//     reflected direction (octahedral, RG32F) and the surface (its view z, roughness, +2 where a ray is wanted: the screen left a share
//     missing on a surface no rougher than MaxRoughness). The two ray images are shared with the helper (RG32F: the RT core's formats).
//   2 the helper traces those rays (closest hit, then one sun shadow ray from the hit) and writes the hit image (RGBA16F), at the RT
//     core's late exchange (RtCore::Exchange, inside ReflectionsEffect::RtTrace).
//   3 Resolve (here): shades the hits (the screen at the hit, the cube toward it, or the estimate), then the old fallbacks for what is
//     left, the firefly cap and the diagnostics: the rest of ReflectionsTrace, line for line. Its output is the trace's output.
// Without the helper this frame the plain ReflectionsTrace runs instead (exactly as before).
// Spaces: camera-relative world (world axes, origin at the camera) for every ray start and direction.

layout(set = 1, binding = 17) uniform texture2D t_rtMarch;    // RayGen out 0: the march's colour (linear rgb), confidence (a)
layout(set = 1, binding = 18) uniform texture2D t_rtDir;      // RayGen out 1: the reflected direction (world axes), octahedral (xy)
layout(set = 1, binding = 19) uniform texture2D t_rtRay;      // RayGen out 2: x the surface's view z (0: nothing reflects), y roughness (+2: a ray)
layout(set = 1, binding = 20) uniform texture2D t_rtHit;      // the helper: x hit distance (-1 no ray, 0 nothing within Distance),
                                                              // y custom index (1 land, 0 object), z sun visible at the hit (0..1), w front face

layout(set = 2, binding = 1, std140) uniform RtConstants {
    vec4 rt;    // x 1 = on, y MaxRoughness, z Distance (game units), w DebugView
    vec4 rt2;   // x screen match tolerance (share of the depth), y the estimate's brightness, z sun amount (0 night / indoors), w 1 = per-instance materials
    vec4 sunDir;    // world, toward the sun (TESR_SunDirection)
    vec4 sunColor;  // TESR_SunColor (gamma)
} rtc;

#define RtOn (rtc.rt.x > 0.5)
#define RtMaxRoughness rtc.rt.y
#define RtDistance rtc.rt.z
#define RtDebugView rtc.rt.w
#define RtTolerance rtc.rt2.x

// Octahedral unit vectors (exact enough in RG32F; the helper decodes with the same formula).
vec2 OctEncode(vec3 v) {
    v /= abs(v.x) + abs(v.y) + abs(v.z);
    vec2 e = v.xy;
    if (v.z < 0.0) e = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    return e;
}
vec3 OctDecode(vec2 e) {
    vec3 v = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    return normalize(v);
}

// How much RT counts on a surface of this roughness: 1 up to 75 % of MaxRoughness, 0 at it.
float RtFade(float roughness) { return 1.0 - smoothstep(0.75 * RtMaxRoughness, RtMaxRoughness, roughness); }

// The cheap shading of a hit neither the screen nor the cube can show (FORK-RT-REFLECTIONS.md "Hit shading"): a grey surface facing the
// ray, lit by the surroundings cube's blurriest mip round it (the light the place has) and, outdoors by day, the sun as far as the
// helper's shadow ray saw it. The surface's own colour and normal need the RT core's per-instance records (requested; rt2.w).
const float RtGuessAlbedo = 0.4;   // as GridAlbedo: desert ground, plaster
vec3 RtShadeHit(vec3 dir, float sunVisible) {
    vec3 normal = -dir;
    vec3 light = CubeOn > 0.0 ? EnvAlong(normal, 1.0).rgb : SkyAlong(vec3(0.0, 0.0, 1.0)) * 0.5;
    float sunAmount = rtc.rt2.z;
    if (sunAmount > 0.0) light += linearize(rtc.sunColor.rgb) * saturate(dot(normal, normalize(rtc.sunDir.xyz))) * sunAmount * sunVisible;
    return vec3(RtGuessAlbedo) * light * rtc.rt2.y;
}
