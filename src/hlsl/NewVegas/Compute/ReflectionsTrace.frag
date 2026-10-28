#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = (vpos + 0.5) * LowTexel;
    vec2 pixelUV = uv - 0.5 * (LowTexel - cb.reciprocalResolution.xy);
    float depth = readDepthLod(pixelUV);
    vec3 N = ViewNormal(pixelUV);
    float roughness;
    float amount = Reflectivity(pixelUV, depth, N, roughness);
    if (amount <= 0.0) { o_color = vec4(0.0); return; }

    vec3 position = ViewPosition(pixelUV, depth);
    vec3 V = normalize(position);

    float a = roughness * roughness;
    float u1 = GradientNoise(vpos), u2 = GradientNoise(vpos + vec2(37.0, 17.0));
    float cosTheta = sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 6.2831853 * u2;
    vec3 tangent = normalize(abs(N.z) < 0.999 ? cross(N, vec3(0, 0, 1)) : cross(N, vec3(1, 0, 0)));
    vec3 bitangent = cross(N, tangent);
    vec3 H = normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + N * cosTheta);
    vec3 R = reflect(V, H);
    if (dot(R, N) <= 0.0) R = reflect(V, N);

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
        if (p.z <= nearZ || any(lessThanEqual(at, vec2(0.0))) || any(greaterThanEqual(at, vec2(1.0)))) searching = false;
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
        if (!IsWeapon(hit) && dot(hitNormal, R) < 0.2) {
            colour = linearize(textureLod(sampler2D(t_rendered, LINEAR_S), hit, 0.0).rgb);
            vec2 edge = min(hit, 1.0 - hit);
            confidence = saturate(min(edge.x, edge.y) * 12.0) * (1.0 - smoothstep(0.75, 1.0, hi / RayLength));
        }
    }
    vec3 sum = colour * confidence;
    vec3 worldR = ViewToWorldDir(R);
    float screenShare = confidence, cubeShare = 0.0, gridShare = 0.0, skyShare = 0.0;
    vec4 grid = vec4(0.0), env = vec4(0.0);
    if (CubeOn > 0.0 && confidence < 1.0) env = EnvAlong(worldR, roughness);
    float gridFirst = smoothstep(0.15, 0.35, roughness);
    bool gridUseful = (gridFirst > 0.0 || env.a < 1.0) && !(SkyFallback > 0.0 && RoofMaskOn < 0.5 && worldR.z >= 0.15);
    if (GridOn > 0.0 && ((confidence < 1.0 && gridUseful) || abs(DebugView - 5.0) < 0.5)) grid = GridAlong(ViewToWorldDir(position), worldR);
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
        skyShare *= mix(1.0, mix(cb.sky.y, 1.0, textureLod(sampler2D(t_skyVis, LINEAR_S), uv, 0.0).b), cb.sky.x);
        sum += SkyAlong(worldR) * skyShare;
        confidence += skyShare;
    }
    colour = sum / max(confidence, 1.0e-4);
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
