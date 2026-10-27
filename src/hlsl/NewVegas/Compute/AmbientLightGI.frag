// UNOFFICIAL 2026-10-27 (NVR DXVK, deep/fork2-ssgi) [Shaders.AmbientLight.Main] ComputeGI: AmbientLight's Estimate pass with one bounce of
// light from what is on screen, from the same horizon search (effects/AmbientLight.cpp ExtDraw, in Estimate's place).
//
// The occlusion: Estimate's GTAO, line for line (the same slices, steps, taps, horizons and arc integral), so with ComputeGIMips off and
// ComputeGIQuality 0 it is Estimate's own value (the GI input's level 0 holds the prep's depth exactly). ComputeGIThinOcclusion (gi.w):
// the occlusion from the bitmask below instead (thin things then hide only what lies within the thickness behind them).
// The bounce light: a visibility bitmask per slice (Therrien, Levesque, Gilet 2023, "Screen Space Indirect Lighting with Visibility
// Bitmask"): 32 sectors of the slice's hemisphere; each tap covers the sectors from its surface's front to its front pushed back by
// ComputeGIThickness (gi.x), and the sectors a tap is the FIRST to cover (nearest taps first) add that tap's light. The sectors are spaced
// so each holds 1/32 of the slice's cosine-weighted hemisphere (t = (sin(theta - n) + 1) / 2; theta from the view vector, n the
// normal's), so a count of sectors is a cosine-weighted share and no acos is needed per tap. The falloff with distance (GTAO's weight) is
// a chance per tap (blue noise, new each frame): a sector is covered or not. Integer bit operations: not possible in ps_3_0.
//
// Taps read the GI input (AmbientLightGIPrefilter.comp): a storage buffer, 8 x 8 tiles, uvec2 per texel = (view depth, light sqrt
// 11:11:10), 4 levels; a tap d half-resolution texels away reads level floor(log2(d) - gi.y) (0-3; gi.y < 0: level 0 only), so far taps
// share cache lines (XeGTAO's depth MIP idea). 8 bytes a tap where Estimate reads the 16-byte prep.
//
// Output as Estimate's into NVR_AmbientLight0: a = the visibility after the same OcclusionStrength curve and fade, rgb = the bounce light
// (linear, x the fade). The blurs, AmbientLightGIAccumulate.frag and AmbientLightApply (-DNVR_GI_APPLY) carry it on.
// GI_SLICES x GI_STEPS from variants.txt: q0 2 x 4 (Estimate's), q1 3 x 6, q2 4 x 8.
#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable
#define NVR_GI 1
#include "AmbientLight.glsl"

#ifndef GI_SLICES
#define GI_SLICES 2
#endif
#ifndef GI_STEPS
#define GI_STEPS 4
#endif

layout(set = 1, binding = 0) uniform texture2D t_prep;       // NVR_ALPrep (POINT): the centre's view depth and normal
layout(set = 1, binding = 1) uniform texture2D t_blueNoise;  // TESR_BlueNoiseSampler (POINT, WRAP)
layout(set = 1, binding = 2, std430) readonly restrict buffer GIInput { uvec2 texels[]; } gin;   // buffer slot 1

layout(location = 0) out vec4 o_color;

vec3 UnpackLight(uint p) {
    vec3 s = vec3(float(p & 0x7FFu) * (1.0 / 2047.0), float((p >> 11) & 0x7FFu) * (1.0 / 2047.0), float(p >> 22) * (1.0 / 1023.0));
    return s * s;
}

uvec2 GITexel(uvec2 q, int mip) {
    uint i = pc.giBase[mip] + ((q.y >> 3) * pc.giTiles[mip] + (q.x >> 3)) * 64u + ((q.y & 7u) << 3) + (q.x & 7u);
    return gin.texels[i];
}

// The sectors from t0 to t1 (0..1 across the slice's hemisphere), rounded to whole sectors: a surface that covers less than half a
// sector covers none (a flat floor's own taps lie on its tangent plane, at the hemisphere's edge, and must not light it).
uint Sectors(float t0, float t1) {
    uint a = uint(clamp(t0, 0.0, 1.0) * 32.0 + 0.5);
    uint b = uint(clamp(t1, 0.0, 1.0) * 32.0 + 0.5);
    uint count = b > a ? b - a : 0u;
    return count == 0u ? 0u : ((0xFFFFFFFFu >> (32u - count)) << a);
}

// sin(theta - n) for a direction with cos(theta) = c (to the view vector) on the given side, clamped to the hemisphere round the normal.
float SliceSin(float c, float side, float cosN, float sinN) {
    float s = side * sqrt(max(1.0 - c * c, 0.0));
    float sinD = s * cosN - c * sinN;
    float cosD = c * cosN + s * sinN;
    return cosD < 0.0 ? (sinD >= 0.0 ? 1.0 : -1.0) : sinD;
}

float SliceArc(float horizonCos0, float horizonCos1, float n, float cosNormal, float sinN) {
    float h0 = n + clamp(-acos(clamp(horizonCos1, -1.0, 1.0)) - n, -1.5707963, 1.5707963);
    float h1 = n + clamp(acos(clamp(horizonCos0, -1.0, 1.0)) - n, -1.5707963, 1.5707963);
    return (cosNormal + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) * 0.25 + (cosNormal + 2.0 * h1 * sinN - cos(2.0 * h1 - n)) * 0.25;
}

vec4 GI(vec2 uv) {
    vec4 center = pointLod(t_prep, uv);
    vec3 origin = PositionFromViewZ(uv, center.x);
    float fade = smoothstep(NearFade, NearFade * 2.0, origin.z) * (1.0 - smoothstep(DrawDistance * 0.7, DrawDistance, origin.z));
    if (fade <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);

    vec3 normal = OctDecode(Unpack2(center.y));
    vec3 viewVec = normalize(-origin);
    vec3 noise = textureLod(sampler2D(t_blueNoise, NVR_SAMPLER(pc.pointWrap)), uv / (256.0 * cb.alLayout.zw), 0.0).xyz;
    float turn = cb.alTemporal.x > 0.0 ? cb.alTemporal.z : 0.0;
    noise.xy = fract(noise.xy + turn * vec2(0.618034, 0.754878));
    noise.z = fract(noise.z + turn * 0.569840);
    vec2 screenRadius = Radius * vec2(cb.projectionTransform[0][0], cb.projectionTransform[1][1]) / (2.0 * origin.z);
    screenRadius *= min(1.0, MaxScreenRadius / screenRadius.x);
    float falloffStart = 0.4 * Radius;
    float invFalloff = 1.0 / (Radius - falloffStart);
    vec2 invTexel = 1.0 / cb.alLayout.zw;   // the half-resolution size, as Estimate works it out (its snapping, bit for bit)
    float thickness = max(pc.gi.x, 1.0);
    bool mips = pc.gi.y >= 0.0;

    if (DebugView > 2.5 && DebugView < 3.5) return vec4(normal * 0.5 + 0.5, 1.0);
    if (DebugView > 3.5 && DebugView < 4.5) return vec4(origin.z / 2000.0, fract(origin.z / 100.0), 0.0, 1.0);
    if (DebugView > 4.5 && DebugView < 5.5) return vec4(screenRadius * 10.0, Radius / 1000.0, 1.0);

    float visibility = 0.0, weightSum = 0.0;      // GTAO (Estimate's)
    float openSum = 0.0;                          // the bitmask's open sectors, each slice weighed as GTAO weighs it
    vec3 light = vec3(0.0);
    float firstCos = 0.0, firstLow = 0.0;
    [[unroll]] for (int slice = 0; slice < GI_SLICES; slice++) {
        float phi = (float(slice) + noise.x) * (3.14159265 / float(GI_SLICES));
        vec2 omega = vec2(cos(phi), -sin(phi));
        vec3 direction = vec3(cos(phi), sin(phi), 0.0);
        vec3 orthoDirection = direction - dot(direction, viewVec) * viewVec;
        vec3 axis = normalize(cross(orthoDirection, viewVec));
        vec3 projectedNormal = normal - axis * dot(normal, axis);
        float projectedLength = length(projectedNormal) + 1e-5;
        float signNormal = dot(orthoDirection, projectedNormal) >= 0.0 ? 1.0 : -1.0;
        float cosNormal = saturate(dot(projectedNormal, viewVec) / projectedLength);
        float n = signNormal * acos(cosNormal);
        float sinN = sin(n);
        float lowCos0 = cos(n + 1.5707963 - cb.alEdges.y), lowCos1 = cos(n - 1.5707963 + cb.alEdges.y);
        float horizonCos0 = lowCos0, horizonCos1 = lowCos1;
        uint covered = 0u;
        vec3 sliceLight = vec3(0.0);
        [[unroll]] for (int k = 0; k < GI_STEPS; k++) {
            float s = (float(k) + fract(noise.y + float(k) * 0.618034)) / float(GI_STEPS);
            s *= s;
            vec2 offset = omega * screenRadius * max(s, 2.0 * cb.alLayout.z);
            int mip = mips ? clamp(int(floor(log2(max(length(offset * invTexel), 1.0)) - pc.gi.y)), 0, 3) : 0;
            uvec2 size = max(pc.giSize.xy >> uint(mip), uvec2(1u));
            [[unroll]] for (int side = 0; side < 2; side++) {
                vec2 sampleUV = uv + (side == 0 ? offset : -offset);
                float onScreen = step(0.0, sampleUV.x) * step(sampleUV.x, 1.0) * step(0.0, sampleUV.y) * step(sampleUV.y, 1.0);
                // The level-0 texel (as Estimate snaps to the prep's), then its parent at this level (the prefilter's 2 x 2 children rule).
                uvec2 q = min(uvec2(floor(clamp(sampleUV, 0.0, 1.0) * invTexel)), pc.giSize.xy - 1u);
                uvec2 p = min(q >> uint(mip), size - 1u);
                uvec2 texel = GITexel(p, mip);
                vec2 tapUV = (vec2(p) + 0.5) * float(1 << mip) * cb.alLayout.zw;
                vec3 delta = PositionFromViewZ(tapUV, uintBitsToFloat(texel.x)) - origin;
                float dist = length(delta) + 1e-4;
                vec3 dir = delta * (1.0 / dist);
                float weight = saturate((Radius - dist) * invFalloff) * onScreen * step(NearFade, origin.z + delta.z);
                float cosTap = dot(dir, viewVec);
                float lowCos = side == 0 ? lowCos0 : lowCos1;
                float sampleCos = mix(lowCos, cosTap, weight);
                if (side == 0) horizonCos0 = max(horizonCos0, sampleCos); else horizonCos1 = max(horizonCos1, sampleCos);
                if (slice == 0 && side == 0 && k == 1) { firstCos = cosTap; firstLow = weight; }
                if (weight > fract(noise.z + float(k * 2 + side) * 0.618034)) {
                    float sideSign = side == 0 ? 1.0 : -1.0;
                    float front = SliceSin(cosTap, sideSign, cosNormal, sinN);
                    float back = SliceSin(dot(normalize(delta - viewVec * thickness), viewVec), sideSign, cosNormal, sinN);
                    uint bits = Sectors(0.5 + 0.5 * min(front, back), 0.5 + 0.5 * max(front, back));
                    uint fresh = bits & ~covered;
                    if (fresh != 0u) sliceLight += float(bitCount(fresh)) * UnpackLight(texel.y);
                    covered |= bits;
                }
            }
        }
        visibility += projectedLength * SliceArc(horizonCos0, horizonCos1, n, cosNormal, sinN);
        float full = projectedLength * SliceArc(lowCos0, lowCos1, n, cosNormal, sinN);
        weightSum += full;
        // The bitmask's shares, each slice weighed as GTAO weighs it (its whole hemisphere's arc).
        openSum += full * (1.0 - float(bitCount(covered)) * (1.0 / 32.0));
        light += full * sliceLight * (1.0 / 32.0);
    }
    if (pc.gi.w > 0.5) visibility = openSum;   // ComputeGIThinOcclusion: the open sectors (both / weightSum below)
    light /= max(weightSum, 1e-4);
    if (DebugView > 5.5) return vec4(visibility / max(weightSum, 1e-4), firstCos * 0.5 + 0.5, firstLow, 1.0);
    visibility = pow(saturate(visibility / max(weightSum, 1e-4)), 1.5 * OcclusionStrength);
    visibility = mix(1.0, visibility, fade);
    return vec4(light * fade, saturate(visibility));
}

void main() { o_color = GI(frameUV()); }
