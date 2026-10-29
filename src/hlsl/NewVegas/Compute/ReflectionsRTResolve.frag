#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"
#include "ReflectionsRT.glsl"

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = (vpos + 0.5) * LowTexel;
    vec2 ray = texelFetch(sampler2D(t_rtRay, POINT_S), texel, 0).xy;
    if (ray.x <= 0.0) { o_color = vec4(0.0); return; }
    vec4 march = texelFetch(sampler2D(t_rtMarch, POINT_S), texel, 0);
    vec4 hitData = texelFetch(sampler2D(t_rtHit, POINT_S), texel, 0);
    vec4 shade = texelFetch(sampler2D(t_rtShade, POINT_S), texel, 0);
    uint hitFlags = uint(hitData.y + 0.5);
    vec3 worldR = OctDecode(texelFetch(sampler2D(t_rtDir, POINT_S), texel, 0).xy);
    bool rayCast = ray.y > 1.5;
    float roughness = ray.y - (rayCast ? 2.0 : 0.0);
    vec2 pixelUV = uv - 0.5 * (LowTexel - cb.reciprocalResolution.xy);
    vec3 surfaceW = ViewToWorldDir(ViewPosition(pixelUV, ray.x));
    vec3 colour = march.rgb;
    float confidence = march.a;

    vec3 sum = colour * confidence;
    float rtScreenShare = 0.0, rtCubeShare = 0.0, rtGuessShare = 0.0;
    float rtT = -1.0;
    float rtInstance = -1.0;
    bool rtOpen = false;
    if (abs(RtDebugView - 3.0) < 0.5) { sum = vec3(0.0); confidence = 0.0; }
    float rtFade = RtFade(roughness);
    if (rayCast && hitData.x >= 0.0 && RtDebugView < 4.5) {
        float missing = (1.0 - confidence) * rtFade;
        if (hitData.x > 0.0) {
            rtT = hitData.x;
            rtInstance = float(hitFlags);
            vec3 hitW = surfaceW + worldR * hitData.x;
            vec3 hitV = WorldToViewDir(hitW);
            vec2 at = ToScreen(hitV);
            bool inView = hitV.z > nearZ && all(greaterThan(at, vec2(0.0))) && all(lessThan(at, vec2(1.0)));
            if (inView && abs(readDepthLod(at) - hitV.z) < max(hitV.z * RtTolerance, 8.0) && !IsWeapon(at)) {
                rtScreenShare = missing;
                sum += linearize(textureLod(sampler2D(t_rendered, LINEAR_S), at, 0.0).rgb) * missing;
            } else {
                vec4 env = !inView && CubeOn > 0.0 ? EnvAlong(normalize(hitW), roughness) : vec4(0.0);
                rtCubeShare = missing * env.a;
                rtGuessShare = missing - rtCubeShare;
                vec3 hitNormal = (hitFlags & 4u) != 0u ? OctDecode(hitData.zw) : -worldR;
                sum += env.rgb * rtCubeShare + RtShadeHit(hitNormal, shade.rgb, shade.a) * rtGuessShare;
            }
            confidence += missing;
        }
        else { rtT = 0.0; rtOpen = rtFade >= 1.0; }
    }
    float screenShare = confidence, cubeShare = 0.0, gridShare = 0.0, skyShare = 0.0;
    vec4 grid = vec4(0.0), env = vec4(0.0);
    if (CubeOn > 0.0 && confidence < 1.0) env = EnvAlong(worldR, roughness);
    float gridFirst = smoothstep(0.15, 0.35, roughness);
    bool gridUseful = (gridFirst > 0.0 || env.a < 1.0) && !(SkyFallback > 0.0 && RoofMaskOn < 0.5 && worldR.z >= 0.15);
    if (GridOn > 0.0 && ((confidence < 1.0 && gridUseful) || abs(DebugView - 5.0) < 0.5)) grid = GridAlong(surfaceW, worldR);
    if (confidence < 1.0 && (CubeOn > 0.0 || GridOn > 0.0)) {
        float missing = 1.0 - confidence;
        float cubeA = missing * env.a, gridA = (missing - cubeA) * grid.a;
        float gridB = missing * grid.a, cubeB = (missing - gridB) * env.a;
        cubeShare = mix(cubeA, cubeB, gridFirst);
        gridShare = mix(gridA, gridB, gridFirst);
        sum += env.rgb * cubeShare + grid.rgb * gridShare;
        confidence += cubeShare + gridShare;
    }
    if (confidence < 1.0 && SkyFallback > 0.0) {
        skyShare = (1.0 - confidence) * smoothstep(0.0, 0.15, worldR.z);
        if (!rtOpen) skyShare *= mix(1.0, mix(cb.sky.y, 1.0, textureLod(sampler2D(t_skyVis, LINEAR_S), uv, 0.0).b), cb.sky.x);
        sum += SkyAlong(worldR) * skyShare;
        confidence += skyShare;
    }
    colour = sum / max(confidence, 1.0e-4);
    if (RtDebugView > 0.5 && abs(RtDebugView - 3.0) > 0.5) {
        if (RtDebugView < 1.5) {
            float marchShare = confidence - rtScreenShare - rtCubeShare - rtGuessShare - cubeShare - gridShare - skyShare;
            colour = vec3(0.0, 1.0, 0.0) * marchShare + vec3(0.0, 1.0, 1.0) * rtScreenShare + vec3(0.0, 0.3, 1.0) * rtCubeShare +
                vec3(1.0, 0.0, 1.0) * rtGuessShare + vec3(0.5) * (cubeShare + gridShare + skyShare);
        }
        else if (RtDebugView < 2.5) colour = rtT < 0.0 ? vec3(0.0) : rtT == 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(1.0 - saturate(rtT / RtDistance) * 0.9);
        else if (RtDebugView > 4.5) {
            float expected = length(surfaceW), t = hitData.x;
            colour = !rayCast || t < 0.0 ? vec3(0.0) : t == 0.0 ? vec3(1.0, 0.0, 0.0) :
                abs(t - expected) < max(4.0, expected * 0.01) ? vec3(0.0, 1.0, 0.0) : t < expected ? vec3(0.0, 0.3, 1.0) : vec3(1.0, 1.0, 0.0);
        }
        else {
            uint k = uint(max(rtInstance, 0.0)) * 2654435761u;
            colour = rtInstance < 0.0 ? vec3(0.0) : vec3(float(k & 255u), float((k >> 8) & 255u), float((k >> 16) & 255u)) / 255.0;
        }
        o_color = vec4(colour, 1.0);
        return;
    }
    if (DebugView > 4.5) {
        if (DebugView < 5.5) colour = grid.rgb;
        else colour = vec3(gridShare, screenShare, cubeShare) + 0.5 * skyShare;
        o_color = vec4(Tame(colour), 1.0);
        return;
    }
    float brightest = max(colour.r, max(colour.g, colour.b));
    colour *= min(1.0, mix(BrightestReflection, 3.0, saturate(roughness * 2.0)) / max(brightest, 1.0e-4));
    o_color = vec4(Tame(colour) * confidence, confidence);
}
