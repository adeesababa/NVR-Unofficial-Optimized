#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"
#include "Counters.glsl"

layout(set = 1, binding = 16, std430) readonly buffer HiZPyramid { vec2 hizCells[]; };

const int HiZLevels = 7;
const int HiZMaxIterations = 64;

vec2 CellDepth(ivec2 c, int L) {
    if (L == 0) { float d = texelFetch(sampler2D(t_depth, POINT_S), c, 0).x * farZ; return vec2(d); }
    uvec2 size0 = ((uvec2(1.0 / cb.reciprocalResolution.xy + 0.5) + 127u) / 128u) * 64u;
    uint k = uint(L - 1), offset = 0u;
    for (uint i = 0u; i < k; i++) offset += (size0.x >> i) * (size0.y >> i);
    vec2 v = hizCells[offset + uint(c.y) * (size0.x >> k) + uint(c.x)];
    if (cb.layoutData.w > 0.5) v = vec2(min(v.x, v.y), max(v.x, v.y));
    return v * farZ;
}

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = (vpos + 0.5) * LowTexel;
    vec2 pixelUV = uv - 0.5 * (LowTexel - cb.reciprocalResolution.xy);
    float depth = readDepthLod(pixelUV);
    vec3 N = ViewNormal(pixelUV);
    float roughness;
    float amount = Reflectivity(pixelUV, depth, N, roughness);
    if (amount <= 0.0) { NVR_COUNT(11); o_color = vec4(0.0); return; }

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

    vec2 fullSize = 1.0 / cb.reciprocalResolution.xy;
    float tStart = 2.0;
    float tEnd = RayLength + 2.0;
    if (R.z < 0.0) tEnd = min(tEnd, (nearZ - position.z) / R.z * 0.999);
    float hitT = -1.0;
    vec2 hit = vec2(0.0);
    int endRule = 5;
    float nFacing = 0.0, nPlane = 0.0, nThick = 0.0;
    int cellsWalked = 0;
    if (tEnd > tStart) {
        vec3 P0 = position + R * tStart, P1 = position + R * tEnd;
        float k0 = 1.0 / P0.z, k1 = 1.0 / P1.z;
        vec3 Q0 = P0 * k0, Q1 = P1 * k1;
        vec2 S0 = ToScreen(P0) * fullSize, S1 = ToScreen(P1) * fullSize;
        vec2 d = S1 - S0;
        float uMax = 1.0;
        if (d.x > 0.0) uMax = min(uMax, (fullSize.x - 0.01 - S0.x) / d.x); else if (d.x < 0.0) uMax = min(uMax, (0.01 - S0.x) / d.x);
        if (d.y > 0.0) uMax = min(uMax, (fullSize.y - 0.01 - S0.y) / d.y); else if (d.y < 0.0) uMax = min(uMax, (0.01 - S0.y) / d.y);
        bool inside = all(greaterThan(S0, vec2(0.0))) && all(lessThan(S0, fullSize));
        vec2 dirSign = vec2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
        vec2 invD = vec2(abs(d.x) > 1.0e-6 ? 1.0 / d.x : 1.0e12, abs(d.y) > 1.0e-6 ? 1.0 / d.y : 1.0e12);
        float stepZ = abs(R.z) * 2.0 * RayLength / Steps;
        float u = 0.0;
        int level = 0;
        int i = 0;
        if (inside) endRule = 0;
        for (; i < HiZMaxIterations && inside && u < uMax; i++) {
            float size = exp2(float(level));
            vec2 S = S0 + d * u;
            vec2 cell = floor(S / size);
            vec2 border = (cell + max(dirSign, 0.0)) * size + dirSign * 0.002;
            vec2 uBorder = (border - S0) * invD;
            float uExit = min(min(uBorder.x, uBorder.y), uMax);
            float zA = 1.0 / mix(k0, k1, u), zB = 1.0 / mix(k0, k1, uExit);
            float rayNear = min(zA, zB), rayFar = max(zA, zB);
            float tExit = dot(mix(Q0, Q1, uExit) * zB - position, R);
            float sExit = sqrt(max(tExit - 2.0, 0.0) / RayLength);
            float thickness = (max(stepZ * sExit, 20.0) * 1.5 + tExit * 0.03) * ThicknessScale;
            vec2 scene = CellDepth(ivec2(cell), level);
            bool behindSkip = rayNear - scene.y >= thickness && (level == 0 || cb.layoutData.w < 0.5);
            bool skip = rayFar <= scene.x
                || scene.x >= farZ * 0.98
                || behindSkip;
            if (rayFar > scene.x && scene.x < farZ * 0.98 && behindSkip) nThick += 1.0;
            if (!skip && level == 0 && dot(ViewNormal((cell + 0.5) * cb.reciprocalResolution.xy), R) >= 0.2) { skip = true; nFacing += 1.0; }
            if (!skip && level == 0) {
                vec3 scenePoint = ViewPosition((cell + 0.5) * cb.reciprocalResolution.xy, scene.x);
                if (abs(dot(scenePoint - position, N)) < 2.0 + 0.004 * scene.x) { skip = true; nPlane += 1.0; }
            }
            if (skip) {
                u = uExit;
                level = min(level + 1, HiZLevels);
            }
            else if (level > 0) level--;
            else {
                float uHit = zA >= scene.x ? u : clamp((1.0 / scene.x - k0) / (k1 - k0), u, uExit);
                float zHit = 1.0 / mix(k0, k1, uHit);
                hitT = dot(mix(Q0, Q1, uHit) * zHit - position, R);
                hit = clamp(S0 + d * uHit, cell + 0.01, cell + 0.99) * cb.reciprocalResolution.xy;
                endRule = 1;
                break;
            }
        }
        cellsWalked = i;
        if (hitT < 0.0 && inside && i >= HiZMaxIterations && u < uMax) {
            endRule = 4;
            float uNow = u;
            float zNow = 1.0 / mix(k0, k1, uNow);
            float tNow = dot(mix(Q0, Q1, uNow) * zNow - position, R);
            float jitter = GradientNoise(vpos + vec2(11.0, 71.0));
            float lastT = tNow;
            float lastZ = position.z + R.z * tNow;
            float foundT = -1.0;
            for (float k = 1.0; k <= Steps; k += 1.0) {
                float s = (k - 1.0 + jitter) / Steps;
                float t = RayLength * s * s + 2.0;
                if (t <= tNow) continue;
                vec3 p = position + R * t;
                vec2 at = ToScreen(p);
                if (p.z <= nearZ || any(lessThanEqual(at, vec2(0.0))) || any(greaterThanEqual(at, vec2(1.0)))) break;
                float sceneZ = readDepthLod(at);
                float behind = p.z - sceneZ;
                float thickness = (max(abs(p.z - lastZ), 20.0) * 1.5 + t * 0.03) * ThicknessScale;
                if (behind > 0.0 && behind < thickness && sceneZ < farZ * 0.98) { foundT = t; break; }
                lastT = t; lastZ = p.z;
            }
            if (foundT > 0.0) {
                float lo = lastT, hi = foundT;
                for (int r = 0; r < RefineSteps; r++) {
                    float m = 0.5 * (lo + hi);
                    vec3 q = position + R * m;
                    if (q.z > readDepthLod(ToScreen(q))) hi = m; else lo = m;
                }
                hitT = hi;
                hit = ToScreen(position + R * hi);
                endRule = 3;
            }
        }
    }

    vec3 colour = vec3(0.0);
    float confidence = 0.0;
    if (hitT > 0.0) {
        vec3 hitNormal = ViewNormal(hit);
        if (!IsWeapon(hit) && dot(hitNormal, R) < 0.2) {
            colour = linearize(textureLod(sampler2D(t_rendered, LINEAR_S), hit, 0.0).rgb);
            vec2 edge = min(hit, 1.0 - hit);
            confidence = saturate(min(edge.x, edge.y) * 12.0) * (1.0 - smoothstep(0.75, 1.0, hitT / RayLength));
        }
        else if (endRule == 1 || endRule == 3) endRule = 2;
    }
    NVR_COUNT(0);
    NVR_COUNT(1 + endRule);
    NVR_COUNT_N(7, cellsWalked);
    NVR_COUNT_N(8, nFacing);
    NVR_COUNT_N(9, nPlane);
    NVR_COUNT_N(10, nThick);
    NVR_PROBE(0, vec4(float(endRule), float(cellsWalked), hitT, confidence));
    NVR_PROBE(1, vec4(nFacing, nPlane, nThick, roughness));
    NVR_PROBE(2, vec4(hit, depth, amount));
    if (DebugView > 8.5) {
        vec3 c;
        if (DebugView > 9.5) c = saturate(vec3(nFacing, nPlane, nThick) / 8.0);
        else c = endRule == 1 ? vec3(0, 1, 0) : endRule == 3 ? vec3(0, 1, 1) : endRule == 2 ? vec3(1, 1, 0) :
            endRule == 0 ? vec3(1, 0, 0) : endRule == 4 ? vec3(1, 0, 1) : vec3(0, 0, 1);
        o_color = vec4(Tame(c * 0.6), 1.0);
        return;
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
