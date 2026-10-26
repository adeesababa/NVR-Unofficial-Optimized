// UNOFFICIAL 2026-10-26 (NVR DXVK, deep/fork-hiz): Reflections technique 1 (Trace, half resolution) with a HIERARCHICAL ray march on the
// Hi-Z pyramid (HiZBuild.comp, core/HiZ.cpp) instead of the fixed growing steps. [Shaders.Reflections.Main] HiZ. Everything before and
// after the march is ReflectionsTrace.frag's, line for line (same roughness spread, same fallbacks, same firefly cap); only how the
// first hit along the reflected ray is found differs:
//   ReflectionsTrace: Steps samples at t = MaxDistance * s^2 (s = 0..1), then 5 bisection steps; can step over thin things.
//   here: the ray is walked in screen space, cell by cell. A cell of a coarse level whose nearest depth is behind the whole stretch of the
//   ray inside it (or whose farthest depth plus the allowed thickness is in front of it) cannot hold a hit and is skipped whole; the
//   walk climbs a level after each skip and goes down a level where a hit is possible. On the finest level (the full-resolution depth
//   itself) a pixel is a hit when the ray passes behind it by less than the thickness: the trace's own rule, with its own thickness
//   (the same growth along the ray), at every pixel the ray crosses instead of every Steps-th sample. No bisection is needed.
// Same reach (MaxDistance), same stop conditions (off the screen, behind the near plane, the sky never a hit, the weapon rejected).
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"

layout(set = 1, binding = 16) uniform texture2D t_hiz;   // NVR_HiZ: r nearest, g farthest linear depth / farZ; level k = 2^(k+1) pixels

const int HiZLevels = 7;           // the pyramid's levels; walk levels 0 (full resolution) .. HiZLevels
const int HiZMaxIterations = 96;   // cells visited at most (a miss after that, as a ray past its last step)

// Nearest and farthest depth (game units) of cell c at walk level L (0 = TESR_DepthBuffer itself, L >= 1 = NVR_HiZ level L - 1).
vec2 CellDepth(ivec2 c, int L) {
    if (L == 0) { float d = texelFetch(sampler2D(t_depth, POINT_S), c, 0).x * farZ; return vec2(d); }
    return texelFetch(sampler2D(t_hiz, POINT_S), c, L - 1).xy * farZ;
}

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

    // ---- The hierarchical march ----
    vec2 fullSize = 1.0 / cb.reciprocalResolution.xy;
    float tStart = 2.0;                        // the trace's first sample sits 2 units out (+ its jitter)
    float tEnd = RayLength + 2.0;              // its last at MaxDistance + 2
    if (R.z < 0.0) tEnd = min(tEnd, (nearZ - position.z) / R.z * 0.999);   // stay in front of the near plane (p.z > nearZ)
    float hitT = -1.0;
    vec2 hit = vec2(0.0);
    if (tEnd > tStart) {
        vec3 P0 = position + R * tStart, P1 = position + R * tEnd;
        float k0 = 1.0 / P0.z, k1 = 1.0 / P1.z;   // 1/z is linear in screen space
        vec3 Q0 = P0 * k0, Q1 = P1 * k1;
        vec2 S0 = ToScreen(P0) * fullSize, S1 = ToScreen(P1) * fullSize;   // full-resolution pixels
        vec2 d = S1 - S0;
        // Where the ray leaves the screen (the trace stops there: at <= 0 or >= 1).
        float uMax = 1.0;
        if (d.x > 0.0) uMax = min(uMax, (fullSize.x - 0.01 - S0.x) / d.x); else if (d.x < 0.0) uMax = min(uMax, (0.01 - S0.x) / d.x);
        if (d.y > 0.0) uMax = min(uMax, (fullSize.y - 0.01 - S0.y) / d.y); else if (d.y < 0.0) uMax = min(uMax, (0.01 - S0.y) / d.y);
        bool inside = all(greaterThan(S0, vec2(0.0))) && all(lessThan(S0, fullSize));
        vec2 dirSign = vec2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
        vec2 invD = vec2(abs(d.x) > 1.0e-6 ? 1.0 / d.x : 1.0e12, abs(d.y) > 1.0e-6 ? 1.0 / d.y : 1.0e12);
        // The trace's thickness at distance t along the ray: its step there (t = L s^2 grows by about 2 L s / Steps) projected on z.
        float stepZ = abs(R.z) * 2.0 * RayLength / Steps;
        float u = 0.0;
        int level = 0;
        for (int i = 0; i < HiZMaxIterations && inside && u < uMax; i++) {
            float size = exp2(float(level));
            vec2 S = S0 + d * u;
            vec2 cell = floor(S / size);
            // The ray's exit from this cell (pushed a hair past the border so the next cell is entered).
            vec2 border = (cell + max(dirSign, 0.0)) * size + dirSign * 0.002;
            vec2 uBorder = (border - S0) * invD;
            float uExit = min(min(uBorder.x, uBorder.y), uMax);
            float zA = 1.0 / mix(k0, k1, u), zB = 1.0 / mix(k0, k1, uExit);
            float rayNear = min(zA, zB), rayFar = max(zA, zB);
            float tExit = dot(mix(Q0, Q1, uExit) * zB - position, R);
            float sExit = sqrt(max(tExit - 2.0, 0.0) / RayLength);
            float thickness = (max(stepZ * sExit, 20.0) * 1.5 + tExit * 0.03) * ThicknessScale;
            vec2 scene = CellDepth(ivec2(cell), level);   // nearest, farthest
            bool skip = rayFar <= scene.x                  // the ray is in front of everything in the cell
                || scene.x >= farZ * 0.98                  // sky only: never a hit
                || rayNear - scene.y >= thickness;         // behind everything by more than the thickness
            if (skip) {
                u = uExit;
                level = min(level + 1, HiZLevels);
            }
            else if (level > 0) level--;
            else {
                // A hit in this pixel: where the ray passes behind its depth (or where it entered, already behind).
                float uHit = zA >= scene.x ? u : clamp((1.0 / scene.x - k0) / (k1 - k0), u, uExit);
                float zHit = 1.0 / mix(k0, k1, uHit);
                hitT = dot(mix(Q0, Q1, uHit) * zHit - position, R);
                hit = clamp(S0 + d * uHit, cell + 0.01, cell + 0.99) * cb.reciprocalResolution.xy;
                break;
            }
        }
    }

    vec3 colour = vec3(0.0);
    float confidence = 0.0;
    if (hitT > 0.0) {
        vec3 hitNormal = ViewNormal(hit);
        // Not the weapon (it is not where the depth says, for the world), not the back of something.
        if (!IsWeapon(hit) && dot(hitNormal, R) < 0.2) {
            colour = linearize(textureLod(sampler2D(t_rendered, LINEAR_S), hit, 0.0).rgb);
            vec2 edge = min(hit, 1.0 - hit);
            confidence = saturate(min(edge.x, edge.y) * 12.0) * (1.0 - smoothstep(0.75, 1.0, hitT / RayLength));
        }
    }
    // What the ray did not find on screen: the surroundings cube and (GridFallback) the light grid along it, then (outdoors)
    // the sky if it points up into it. Each fills only the share still missing (premultiplied sums).
    vec3 sum = colour * confidence;
    vec3 worldR = ViewToWorldDir(R);
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
        skyShare *= mix(1.0, mix(cb.sky.y, 1.0, textureLod(sampler2D(t_skyVis, LINEAR_S), uv, 0.0).b), cb.sky.x);
        sum += SkyAlong(worldR) * skyShare;
        confidence += skyShare;
    }
    colour = sum / max(confidence, 1.0e-4);
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
