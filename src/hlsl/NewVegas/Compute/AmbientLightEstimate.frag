#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable
#include "AmbientLight.glsl"

#ifdef NVR_GI
#define AL_SLICES GI_SLICES
#define AL_STEPS GI_STEPS
#else
#define AL_SLICES SLICES
#define AL_STEPS STEPS
#endif

layout(set = 1, binding = 0) uniform texture2D t_prep;
layout(set = 1, binding = 1) uniform texture2D t_blueNoise;
#ifdef NVR_GI
layout(set = 1, binding = 2) uniform texture2D t_light;
#endif

layout(location = 0) out vec4 o_color;

float SliceArc(float horizonCos0, float horizonCos1, float n, float cosNormal, float sinN) {
    float h0 = n + clamp(-acos(clamp(horizonCos1, -1.0, 1.0)) - n, -1.5707963, 1.5707963);
    float h1 = n + clamp(acos(clamp(horizonCos0, -1.0, 1.0)) - n, -1.5707963, 1.5707963);
    return (cosNormal + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) * 0.25 + (cosNormal + 2.0 * h1 * sinN - cos(2.0 * h1 - n)) * 0.25;
}

#ifdef NVR_GI
uint Sectors(float t0, float t1) {
    uint a = uint(clamp(t0, 0.0, 1.0) * 32.0 + 0.5);
    uint b = uint(clamp(t1, 0.0, 1.0) * 32.0 + 0.5);
    uint count = b > a ? b - a : 0u;
    return count == 0u ? 0u : ((0xFFFFFFFFu >> (32u - count)) << a);
}

float SliceSin(float c, float side, float cosN, float sinN) {
    float s = side * sqrt(max(1.0 - c * c, 0.0));
    float sinD = s * cosN - c * sinN;
    float cosD = c * cosN + s * sinN;
    return cosD < 0.0 ? side : sinD;
}
#endif

vec4 Estimate(vec2 uv) {
    vec4 center = pointLod(t_prep, uv);
    vec3 origin = PositionFromViewZ(uv, center.x);
    float fade = smoothstep(NearFade, NearFade * 2.0, origin.z) * (1.0 - smoothstep(DrawDistance * 0.7, DrawDistance, origin.z));
    if (fade <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);

    vec3 normal = OctDecode(Unpack2(center.y));
    vec3 viewVec = normalize(-origin);
    vec3 noise = textureLod(sampler2D(t_blueNoise, NVR_SAMPLER(pc.pointWrap)), uv / (256.0 * cb.alLayout.zw), 0.0).xyz;
    noise.xy = fract(noise.xy + (cb.alTemporal.x > 0.0 ? cb.alTemporal.z : 0.0) * vec2(0.618034, 0.754878));
    vec2 screenRadius = Radius * vec2(cb.projectionTransform[0][0], cb.projectionTransform[1][1]) / (2.0 * origin.z);
    screenRadius *= min(1.0, MaxScreenRadius / screenRadius.x);
    float falloffStart = 0.4 * Radius;
    float invFalloff = 1.0 / (Radius - falloffStart);
    vec2 invTexel = 1.0 / cb.alLayout.zw;
#ifdef NVR_GI
    float chance = fract(noise.z + (cb.alTemporal.x > 0.0 ? cb.alTemporal.z : 0.0) * 0.569840);
    float thickness = max(pc.gi.x, 1.0);
    float openSum = 0.0;
    vec3 light = vec3(0.0);
#endif

    if (DebugView > 2.5 && DebugView < 3.5) return vec4(normal * 0.5 + 0.5, 1.0);
    if (DebugView > 3.5 && DebugView < 4.5) return vec4(origin.z / 2000.0, fract(origin.z / 100.0), 0.0, 1.0);
    if (DebugView > 4.5 && DebugView < 5.5) return vec4(screenRadius * 10.0, Radius / 1000.0, 1.0);
    float visibility = 0.0, weightSum = 0.0;
    vec3 bounce = vec3(0.0);
    float firstCos = 0.0, firstLow = 0.0;
    [[unroll]] for (int slice = 0; slice < AL_SLICES; slice++) {
        float phi = (float(slice) + noise.x) * (3.14159265 / float(AL_SLICES));
        vec2 omega = vec2(cos(phi), -sin(phi));
        vec3 direction = vec3(cos(phi), sin(phi), 0.0);
        vec3 orthoDirection = direction - dot(direction, viewVec) * viewVec;
        vec3 axis = normalize(cross(orthoDirection, viewVec));
        vec3 projectedNormal = normal - axis * dot(normal, axis);
        float projectedLength = length(projectedNormal) + 1e-5;
        float signNormal = dot(orthoDirection, projectedNormal) >= 0.0 ? 1.0 : -1.0;
        float cosNormal = saturate(dot(projectedNormal, viewVec) / projectedLength);
        float n = signNormal * acos(cosNormal);
        float lowCos0 = cos(n + 1.5707963 - cb.alEdges.y), lowCos1 = cos(n - 1.5707963 + cb.alEdges.y);
        float horizonCos0 = lowCos0, horizonCos1 = lowCos1;
#ifdef NVR_GI
        float sinNGI = sin(n);
        uint covered = 0u;
        vec3 sliceLight = vec3(0.0);
#endif
        [[unroll]] for (int k = 0; k < AL_STEPS; k++) {
            float s = (float(k) + fract(noise.y + float(k) * 0.618034)) / float(AL_STEPS);
            s *= s;
            vec2 offset = omega * screenRadius * max(s, 2.0 * cb.alLayout.z);
            [[unroll]] for (int side = 0; side < 2; side++) {
                vec2 sampleUV = uv + (side == 0 ? offset : -offset);
                float onScreen = step(0.0, sampleUV.x) * step(sampleUV.x, 1.0) * step(0.0, sampleUV.y) * step(sampleUV.y, 1.0);
                sampleUV = (floor(clamp(sampleUV, 0.0, 1.0) * invTexel) + 0.5) * cb.alLayout.zw;
                vec4 texel = pointLod(t_prep, sampleUV);
                vec3 delta = PositionFromViewZ(sampleUV, texel.x) - origin;
                float dist = length(delta) + 1e-4;
                vec3 dir = delta * (1.0 / dist);
                float weight = saturate((Radius - dist) * invFalloff) * onScreen * step(NearFade, origin.z + delta.z);
                float lowCos = side == 0 ? lowCos0 : lowCos1;
                float sampleCos = mix(lowCos, dot(dir, viewVec), weight);
                if (side == 0) horizonCos0 = max(horizonCos0, sampleCos); else horizonCos1 = max(horizonCos1, sampleCos);
                if (slice == 0 && side == 0 && k == 1) { firstCos = dot(dir, viewVec); firstLow = weight; }
#ifdef NVR_GI
                if (weight > fract(chance + float(k * 2 + side) * 0.618034)) {
                    float sideSign = side == 0 ? 1.0 : -1.0;
                    float front = SliceSin(dot(dir, viewVec), sideSign, cosNormal, sinNGI);
                    float back = SliceSin(dot(normalize(delta - viewVec * thickness), viewVec), sideSign, cosNormal, sinNGI);
                    uint bits = Sectors(0.5 + 0.5 * min(front, back), 0.5 + 0.5 * max(front, back));
                    uint fresh = bits & ~covered;
                    if (fresh != 0u) {
                        float facing = saturate(-dot(OctDecode(Unpack2(texel.y)), dir) * 8.0);
                        vec3 l = pointLod(t_light, sampleUV).rgb;
                        sliceLight += (float(bitCount(fresh)) * facing) * (l * l);
                    }
                    covered |= bits;
                }
#endif
            }
        }
        float sinN = sin(n);
        visibility += projectedLength * SliceArc(horizonCos0, horizonCos1, n, cosNormal, sinN);
        weightSum += projectedLength * SliceArc(lowCos0, lowCos1, n, cosNormal, sinN);
#ifdef NVR_GI
        float full = projectedLength * SliceArc(lowCos0, lowCos1, n, cosNormal, sinN);
        openSum += full * (1.0 - float(bitCount(covered)) * (1.0 / 32.0));
        light += full * sliceLight * (1.0 / 32.0);
#endif
    }
#ifdef NVR_GI
    if (pc.gi.w > 0.5) visibility = openSum;
    bounce = light / max(weightSum, 1e-4);
    if (DebugView > 5.5 && DebugView < 6.5) return vec4(visibility / max(weightSum, 1e-4), firstCos * 0.5 + 0.5, firstLow, 1.0);
#else
    if (DebugView > 5.5) return vec4(visibility / max(weightSum, 1e-4), firstCos * 0.5 + 0.5, firstLow, 1.0);
#endif
    visibility = pow(saturate(visibility / max(weightSum, 1e-4)), 1.5 * OcclusionStrength);
    visibility = mix(1.0, visibility, fade);
#ifdef NVR_GI
    return vec4(bounce * fade, saturate(visibility));
#else
    return vec4(bounce * (BounceScale * fade / float(SLICES * STEPS * 2)), saturate(visibility));
#endif
}

void main() { o_color = Estimate(frameUV()); }
