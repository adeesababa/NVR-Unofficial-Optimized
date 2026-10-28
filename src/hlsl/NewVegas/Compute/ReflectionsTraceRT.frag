// UNOFFICIAL 2026-10-28 (NVR DXVK, RT reflections; docs/next/FORK-RT-REFLECTIONS.md): Reflections technique 1 (Trace, half resolution)
// with ray queries where the screen-space march misses. ReflectionsTrace.frag line for line, plus the blocks marked RT: a ray that the
// screen did not fully answer (confidence below 1), on a surface no rougher than [Shaders.Reflections.RT] MaxRoughness, is cast into
// the RT core's TLAS (ReflectionsRT.glsl). A hit takes, in this order: the screen at the hit point when the hit is visible there
// (exact: the lit picture), the surroundings cube toward the hit when it is out of view, else the cheap estimate (RtShadeHit). A hit
// fills the whole missing share, so the cube and the sky no longer leak in where something blocks the ray. A miss keeps the old
// fallbacks (cube, light grid, sky). Built for Vulkan 1.2 (SPIR-V 1.5, GL_EXT_ray_query); used only when the RT core is up.
#version 460
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"
#include "ReflectionsRT.glsl"

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = (vpos + 0.5) * LowTexel;
    vec2 pixelUV = uv - 0.5 * (LowTexel - cb.reciprocalResolution.xy);   // a pixel centre in the block
    float depth = readDepthLod(pixelUV);
    vec3 N = ViewNormal(pixelUV);
    float roughness;
    float amount = Reflectivity(pixelUV, depth, N, roughness);
    if (amount <= 0.0) { o_color = vec4(0.0); return; }

    vec3 position = ViewPosition(pixelUV, depth);
    vec3 V = normalize(position);   // eye -> surface

    // Spread the reflected direction by the roughness: a GGX half vector round the normal, a new one every frame.
    float a = roughness * roughness;
    float u1 = GradientNoise(vpos), u2 = GradientNoise(vpos + vec2(37.0, 17.0));
    float cosTheta = sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 6.2831853 * u2;
    vec3 tangent = normalize(abs(N.z) < 0.999 ? cross(N, vec3(0, 0, 1)) : cross(N, vec3(1, 0, 0)));
    vec3 bitangent = cross(N, tangent);
    vec3 H = normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + N * cosTheta);
    vec3 R = reflect(V, H);
    if (dot(R, N) <= 0.0) R = reflect(V, N);   // spread below the surface: the plain mirror direction

    // March: steps that grow along the ray (fine near the surface, coarse far off), the first one shifted by the noise.
    float jitter = GradientNoise(vpos + vec2(11.0, 71.0));
    float lastT = 0.0;
    float lastZ = position.z;
    float hitT = -1.0;
    bool searching = true;
    for (float i = 1.0; i <= Steps && searching; i += 1.0) {
        float s = (i - 1.0 + jitter) / Steps;
        float t = RayLength * s * s + 2.0;
        vec3 p = position + R * t;
        vec2 at = ToScreen(p);
        if (p.z <= nearZ || any(lessThanEqual(at, vec2(0.0))) || any(greaterThanEqual(at, vec2(1.0)))) searching = false;   // off the screen
        else {
            float sceneZ = readDepthLod(at);
            float behind = p.z - sceneZ;
            float thickness = (max(abs(p.z - lastZ), 20.0) * 1.5 + t * 0.03) * ThicknessScale;
            if (behind > 0.0 && behind < thickness && sceneZ < farZ * 0.98) { hitT = t; searching = false; }
            else { lastT = t; lastZ = p.z; }
        }
    }

    vec3 colour = vec3(0.0);
    float confidence = 0.0;
    if (hitT > 0.0) {
        float lo = lastT, hi = hitT;
        for (int r = 0; r < RefineSteps; r++) {
            float m = 0.5 * (lo + hi);
            vec3 q = position + R * m;
            if (q.z > readDepthLod(ToScreen(q))) hi = m; else lo = m;
        }
        vec2 hit = ToScreen(position + R * hi);
        vec3 hitNormal = ViewNormal(hit);
        // Not the weapon (it is not where the depth says, for the world), not the back of something.
        if (!IsWeapon(hit) && dot(hitNormal, R) < 0.2) {
            colour = linearize(textureLod(sampler2D(t_rendered, LINEAR_S), hit, 0.0).rgb);
            vec2 edge = min(hit, 1.0 - hit);
            confidence = saturate(min(edge.x, edge.y) * 12.0) * (1.0 - smoothstep(0.75, 1.0, hi / RayLength));
        }
    }
    // RT: what the screen did not answer, from the ray queries. The hit fills the whole missing share (times the roughness fade);
    // the fallbacks below fill only what is left after it.
    vec3 sum = colour * confidence;
    vec3 worldR = ViewToWorldDir(R);
    float rtScreenShare = 0.0, rtCubeShare = 0.0, rtGuessShare = 0.0, rtKnownShare = 0.0;   // (RT DebugView 1)
    float rtT = -1.0;   // -1 no ray, 0 a ray that met nothing within Distance, else the hit distance (RT DebugView 2)
    int rtInstance = -1;
    bool rtOpen = false;   // a ray went the whole Distance without a hit
    if (abs(RtDebugView - 3.0) < 0.5) { sum = vec3(0.0); confidence = 0.0; }   // RT DebugView 3: the screen march's answer dropped
    float rtFade = 1.0 - smoothstep(0.75 * RtMaxRoughness, RtMaxRoughness, roughness);
    if (RtOn && confidence < 0.999 && rtFade > 0.0) {
        vec3 originW = ViewToWorldDir(position);
        vec3 normalW = ViewToWorldDir(N);
        // Off the surface by a little more than the depth buffer's own error far away (no hit on the surface itself).
        vec3 start = originW + normalW * (1.0 + depth * 0.0015);
        RtHit h = rtTrace(start, worldR, 0.0, RtDistance);
        float missing = (1.0 - confidence) * rtFade;
        if (h.hit) {
            rtT = max(h.t, 1.0e-3);
            rtInstance = h.instance;
            vec3 hitW = start + worldR * h.t;
            vec3 hitV = WorldToViewDir(hitW);   // camera-relative: a direction transform is the whole move
            vec2 at = ToScreen(hitV);
            bool inView = hitV.z > nearZ && all(greaterThan(at, vec2(0.0))) && all(lessThan(at, vec2(1.0)));
            if (inView && abs(readDepthLod(at) - hitV.z) < max(hitV.z * RtTolerance, 8.0) && !IsWeapon(at)) {
                // Visible on screen: the lit picture at the hit itself (what the march finds when no step or thickness error hides it).
                rtScreenShare = missing;
                sum += linearize(textureLod(sampler2D(t_rendered, LINEAR_S), at, 0.0).rgb) * missing;
            } else {
                // Out of view: the surroundings cube toward the hit (it was seen from about here). Hidden behind something in view, or
                // never seen: the estimate (the cube toward a hidden hit would show what hides it).
                float known;
                vec3 guess = RtShadeHit(h, hitW, worldR, known);
                vec4 env = !inView && CubeOn > 0.0 && known < 0.5 ? EnvAlong(normalize(hitW), roughness) : vec4(0.0);
                rtCubeShare = missing * env.a;
                float rest = missing - rtCubeShare;
                if (known > 0.5) rtKnownShare = rest; else rtGuessShare = rest;
                sum += env.rgb * rtCubeShare + guess * rest;
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
    if (GridOn > 0.0 && ((confidence < 1.0 && gridUseful) || abs(DebugView - 5.0) < 0.5)) grid = GridAlong(ViewToWorldDir(position), worldR);
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
    //   orange RT with the hit's own material, grey the old fallbacks (cube, grid, sky), black nothing;
    // 2 the RT hit distance (white near .. dark at Distance), red a ray that met nothing, black no ray (screen enough / too rough);
    // 4 the hit instance (a colour per instance index: checks the TLAS), black no hit.
    if (RtDebugView > 0.5 && abs(RtDebugView - 3.0) > 0.5) {
        if (RtDebugView < 1.5) {
            float march = confidence - rtScreenShare - rtCubeShare - rtGuessShare - rtKnownShare - cubeShare - gridShare - skyShare;
            colour = vec3(0.0, 1.0, 0.0) * march + vec3(0.0, 1.0, 1.0) * rtScreenShare + vec3(0.0, 0.3, 1.0) * rtCubeShare +
                vec3(1.0, 0.0, 1.0) * rtGuessShare + vec3(1.0, 0.5, 0.0) * rtKnownShare + vec3(0.5) * (cubeShare + gridShare + skyShare);
        }
        else if (RtDebugView < 2.5) colour = rtT < 0.0 ? vec3(0.0) : rtT == 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(1.0 - saturate(rtT / RtDistance) * 0.9);
        else {
            uint k = uint(rtInstance) * 2654435761u;
            colour = rtInstance < 0 ? vec3(0.0) : vec3(float(k & 255u), float((k >> 8) & 255u), float((k >> 16) & 255u)) / 255.0;
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
