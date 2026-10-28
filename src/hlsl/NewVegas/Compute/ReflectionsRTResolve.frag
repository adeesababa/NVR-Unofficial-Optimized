// UNOFFICIAL 2026-10-28 (NVR DXVK, RT reflections; ReflectionsRT.glsl has the whole frame): Reflections technique 1 (Trace, half
// resolution), second half, with RT on: reads RayGen's three outputs and the RT helper's hits, and writes what ReflectionsTrace writes.
// A hit fills the whole missing share (times the roughness fade), from the first source that can show it:
//   the screen at the hit when the hit is visible there (exact: the lit picture; what the march misses through its steps / thickness);
//   the surroundings cube toward the hit when it is out of view (the cube was seen from about here);
//   else (hidden behind something in view, or never seen) the estimate, RtShadeHit (the cube toward a hidden hit shows what hides it).
// So a ray that meets something never takes the cube along it or the sky (no sky in a floor under a roof). A ray that met nothing
// within Distance keeps the old fallbacks and takes the sky without the sky-visibility guess. From the fallbacks on: ReflectionsTrace.
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"
#include "ReflectionsRT.glsl"

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = (vpos + 0.5) * LowTexel;
    vec4 dirRough = texelFetch(sampler2D(t_rtDir, POINT_S), texel, 0);
    if (dirRough.a < 0.0) { o_color = vec4(0.0); return; }   // RayGen: nothing reflects here
    vec4 march = texelFetch(sampler2D(t_rtMarch, POINT_S), texel, 0);
    vec4 origin = texelFetch(sampler2D(t_rtOrigin, POINT_S), texel, 0);
    vec4 hitData = texelFetch(sampler2D(t_rtHit, POINT_S), texel, 0);
    vec3 worldR = dirRough.xyz;
    float roughness = dirRough.a;
    vec3 colour = march.rgb;
    float confidence = march.a;

    vec3 sum = colour * confidence;
    float rtScreenShare = 0.0, rtCubeShare = 0.0, rtGuessShare = 0.0;   // (RT DebugView 1)
    float rtT = -1.0;   // -1 no ray, 0 a ray that met nothing within Distance, else the hit distance (RT DebugView 2)
    float rtInstance = -1.0;
    bool rtOpen = false;   // a ray went the whole Distance without a hit
    if (abs(RtDebugView - 3.0) < 0.5) { sum = vec3(0.0); confidence = 0.0; }   // RT DebugView 3: the screen march's answer dropped
    float rtFade = RtFade(roughness);
    if (origin.a > 0.5 && hitData.x >= 0.0) {
        float missing = (1.0 - confidence) * rtFade;
        if (hitData.x > 0.0) {
            rtT = hitData.x;
            rtInstance = hitData.y;
            vec3 hitW = origin.xyz + worldR * hitData.x;
            vec3 hitV = WorldToViewDir(hitW);   // camera-relative: a direction transform is the whole move
            vec2 at = ToScreen(hitV);
            bool inView = hitV.z > nearZ && all(greaterThan(at, vec2(0.0))) && all(lessThan(at, vec2(1.0)));
            if (inView && abs(readDepthLod(at) - hitV.z) < max(hitV.z * RtTolerance, 8.0) && !IsWeapon(at)) {
                rtScreenShare = missing;
                sum += linearize(textureLod(sampler2D(t_rendered, LINEAR_S), at, 0.0).rgb) * missing;
            } else {
                vec4 env = !inView && CubeOn > 0.0 ? EnvAlong(normalize(hitW), roughness) : vec4(0.0);
                rtCubeShare = missing * env.a;
                rtGuessShare = missing - rtCubeShare;
                sum += env.rgb * rtCubeShare + RtShadeHit(worldR, hitData.z) * rtGuessShare;
            }
            confidence += missing;
        }
        else { rtT = 0.0; rtOpen = rtFade >= 1.0; }
    }
    // What the ray did not find on screen: the surroundings cube and (GridFallback) the light grid along it, then (outdoors)
    // the sky if it points up into it. Each fills only the share still missing (premultiplied sums).
    float screenShare = confidence, cubeShare = 0.0, gridShare = 0.0, skyShare = 0.0;   // (DebugView 6)
    vec4 grid = vec4(0.0), env = vec4(0.0);
    if (CubeOn > 0.0 && confidence < 1.0) env = EnvAlong(worldR, roughness);
    float gridFirst = smoothstep(0.15, 0.35, roughness);
    bool gridUseful = (gridFirst > 0.0 || env.a < 1.0) && !(SkyFallback > 0.0 && RoofMaskOn < 0.5 && worldR.z >= 0.15);
    if (GridOn > 0.0 && ((confidence < 1.0 && gridUseful) || abs(DebugView - 5.0) < 0.5)) grid = GridAlong(origin.xyz, worldR);
    if (confidence < 1.0 && (CubeOn > 0.0 || GridOn > 0.0)) {
        float missing = 1.0 - confidence;
        float cubeA = missing * env.a, gridA = (missing - cubeA) * grid.a;   // the cube first
        float gridB = missing * grid.a, cubeB = (missing - gridB) * env.a;   // the grid first
        cubeShare = mix(cubeA, cubeB, gridFirst);
        gridShare = mix(gridA, gridB, gridFirst);
        sum += env.rgb * cubeShare + grid.rgb * gridShare;
        confidence += cubeShare + gridShare;
    }
    if (confidence < 1.0 && SkyFallback > 0.0) {
        skyShare = (1.0 - confidence) * smoothstep(0.0, 0.15, worldR.z);
        // (RT: a ray that met nothing within Distance needs no guess at how much of the sky is hidden.)
        if (!rtOpen) skyShare *= mix(1.0, mix(cb.sky.y, 1.0, textureLod(sampler2D(t_skyVis, LINEAR_S), uv, 0.0).b), cb.sky.x);
        sum += SkyAlong(worldR) * skyShare;
        confidence += skyShare;
    }
    colour = sum / max(confidence, 1.0e-4);
    // RT diagnostics (the effect's DebugView is 6 then, so the composite shows these as they are):
    // 1 where each share came from: green the screen march, cyan RT on screen, blue RT through the cube, magenta RT estimate,
    //   grey the old fallbacks (cube, grid, sky), black nothing;
    // 2 the RT hit distance (white near .. dark at Distance), red a ray that met nothing, black no ray (screen enough / too rough);
    // 4 the hit instance (a colour per instance index: checks the scene), black no hit.
    if (RtDebugView > 0.5 && abs(RtDebugView - 3.0) > 0.5) {
        if (RtDebugView < 1.5) {
            float marchShare = confidence - rtScreenShare - rtCubeShare - rtGuessShare - cubeShare - gridShare - skyShare;
            colour = vec3(0.0, 1.0, 0.0) * marchShare + vec3(0.0, 1.0, 1.0) * rtScreenShare + vec3(0.0, 0.3, 1.0) * rtCubeShare +
                vec3(1.0, 0.0, 1.0) * rtGuessShare + vec3(0.5) * (cubeShare + gridShare + skyShare);
        }
        else if (RtDebugView < 2.5) colour = rtT < 0.0 ? vec3(0.0) : rtT == 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(1.0 - saturate(rtT / RtDistance) * 0.9);
        else {
            uint k = uint(max(rtInstance, 0.0)) * 2654435761u;
            colour = rtInstance < 0.0 ? vec3(0.0) : vec3(float(k & 255u), float((k >> 8) & 255u), float((k >> 16) & 255u)) / 255.0;
        }
        o_color = vec4(colour, 1.0);
        return;
    }
    // Diagnostics: 5 what the light grid gives along every reflected ray, 6 where each share came from.
    if (DebugView > 4.5) {
        if (DebugView < 5.5) colour = grid.rgb;
        else colour = vec3(gridShare, screenShare, cubeShare) + 0.5 * skyShare;
        o_color = vec4(Tame(colour), 1.0);
        return;
    }
    // Fireflies: a lower cap on rough surfaces; everything after this pass averages in the compressed space (Tame).
    float brightest = max(colour.r, max(colour.g, colour.b));
    colour *= min(1.0, mix(BrightestReflection, 3.0, saturate(roughness * 2.0)) / max(brightest, 1.0e-4));
    o_color = vec4(Tame(colour) * confidence, confidence);
}
