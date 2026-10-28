
#ifndef INTERIOR_SHADOWS
    #define INTERIOR_SHADOWS 0
#endif

#if INTERIOR_SHADOWS

#ifndef POINT_SHADOW_SMOOTH
    #define POINT_SHADOW_SMOOTH 0
#endif

#ifndef POINT_SHADOW_FIT
    #define POINT_SHADOW_FIT 0
#endif

#ifndef POINT_SHADOW_MAG
    #define POINT_SHADOW_MAG 0
#endif

#ifndef POINT_SHADOW_EVEN
    #define POINT_SHADOW_EVEN 0
#endif

#ifndef POINT_SHADOW_TEXEL
    #define POINT_SHADOW_TEXEL 0
#endif

float4 NVR_PointShadowLight[6]  : register(c174);
float4 NVR_PointShadowParams    : register(c180);

float4 NVR_PointShadowDebug     : register(c181);
float4 NVR_PointShadowTint[6]   : register(c182);
float4 NVR_PointShadowFocus     : register(c188);
float4 NVR_PointShadowSoft      : register(c189);

samplerCUBE NVR_PointShadowCube0 : register(s8);
samplerCUBE NVR_PointShadowCube1 : register(s11);
samplerCUBE NVR_PointShadowCube2 : register(s12);
samplerCUBE NVR_PointShadowCube3 : register(s13);
samplerCUBE NVR_PointShadowCube4 : register(s14);
samplerCUBE NVR_PointShadowCube5 : register(s15);

float4 PointShadowNear(float4 stored, float lightW) {
    return NVR_PointShadowDebug.y > 0.0f ? smoothstep(0.0f, NVR_PointShadowDebug.y * lightW, stored) : 1.0f;
}

#if POINT_SHADOW_FIT == 1
static const float4 PointShadowTaps[16] = {
    float4( 1.0f, 0.0f, 0.0f, 1.0f), float4(-1.0f, 0.0f, 0.0f, 1.0f), float4(0.0f,  1.0f, 0.0f, 1.0f), float4(0.0f, -1.0f, 0.0f, 1.0f),
    float4(0.0f, 0.0f,  1.0f, 1.0f), float4(0.0f, 0.0f, -1.0f, 1.0f),
    float4( 1.0f,  1.0f,  1.0f, 0.5f), float4(-1.0f, -1.0f,  1.0f, 0.5f), float4( 1.0f, -1.0f, -1.0f, 0.5f), float4(-1.0f,  1.0f, -1.0f, 0.5f),
    float4( 1.0f, 0.0f, 0.0f, 0.5f), float4(-1.0f, 0.0f, 0.0f, 0.5f), float4(0.0f,  1.0f, 0.0f, 0.5f), float4(0.0f, -1.0f, 0.0f, 0.5f),
    float4(0.0f, 0.0f,  1.0f, 0.5f), float4(0.0f, 0.0f, -1.0f, 0.5f) };
#endif

#if POINT_SHADOW_EVEN
static const float4 PointShadowEven[12] = {
    float4(-0.60000f, +0.60000f, +0.20000f, 1.0f), float4(-0.20000f, -0.20000f, -0.20000f, 1.0f),
    float4(+0.20000f, -0.60000f, +0.60000f, 1.0f), float4(+0.60000f, +0.20000f, -0.60000f, 1.0f),
    float4(-0.08452f, -0.25355f, -0.59161f, 1.0f), float4(-0.42258f, +0.42258f, +0.59161f, 1.0f),
    float4(+0.59161f, -0.08452f, -0.08452f, 1.0f), float4(+0.25355f, +0.08452f, +0.42258f, 1.0f),
    float4(-0.59161f, +0.25355f, -0.42258f, 1.0f), float4(+0.42258f, -0.59161f, +0.25355f, 1.0f),
    float4(+0.08452f, +0.59161f, -0.25355f, 1.0f), float4(-0.25355f, -0.42258f, +0.08452f, 1.0f) };
#endif

#if POINT_SHADOW_MAG || POINT_SHADOW_EVEN
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
float PointShadowEven4(samplerCUBE cube, float3 dir, float s, float threshold, float nearK, float lightW, int i) {
    float4 v = float4(
        texCUBElod(cube, float4(dir + s * PointShadowEven[i].xyz, 0.0f)).r,
        texCUBElod(cube, float4(dir + s * PointShadowEven[i + 1].xyz, 0.0f)).r,
        texCUBElod(cube, float4(dir + s * PointShadowEven[i + 2].xyz, 0.0f)).r,
        texCUBElod(cube, float4(dir + s * PointShadowEven[i + 3].xyz, 0.0f)).r);
    return dot((float4)(v <= threshold) * PointShadowKeep(v, nearK, lightW), 1.0f);
}
#endif

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
float PointShadowTexelLevel(samplerCUBE cube, float2 q, float k, float4 M, float4 A, float4 B, float scale, float threshold) {
    float2 g = q / k;
    float2 c = floor(g + 0.5f);
    float2 f = g - c;
    float3 wx = float3(0.5f * (0.5f - f.x) * (0.5f - f.x), 0.75f - f.x * f.x, 0.5f * (0.5f + f.x) * (0.5f + f.x));
    float3 wy = float3(0.5f * (0.5f - f.y) * (0.5f - f.y), 0.75f - f.y * f.y, 0.5f * (0.5f + f.y) * (0.5f + f.y));
    float3 ux = ((c.x + float3(-1.0f, 0.0f, 1.0f)) * k + 0.5f) * scale;
    float3 vy = ((c.y + float3(-1.0f, 0.0f, 1.0f)) * k + 0.5f) * scale;
    float3 rows;
    [unroll] for (int y = 0; y < 3; y++) {
        float4 row = M + vy[y] * B;
        float3 v = float3(texCUBElod(cube, row + ux.x * A).r, texCUBElod(cube, row + ux.y * A).r, texCUBElod(cube, row + ux.z * A).r);
        rows[y] = dot((float3)(v <= threshold) * (float3)(v > 0.0f), wx);
    }
    return dot(rows, wy);
}

float PointShadowTexel(samplerCUBE cube, float3 dir, float major, float s, float threshold, float lightW) {
    float3 ad = abs(dir);
    float xm = (float)(ad.x >= max(ad.y, ad.z));
    float zm = (1.0f - xm) * (float)(ad.z > ad.y);
    float4 A = float4(1.0f - xm, xm, 0.0f, 0.0f) * major;
    float4 B = float4(0.0f, zm, 1.0f - zm, 0.0f) * major;
    float4 M = float4(dir * float3(xm, 1.0f - xm - zm, zm), 0.0f) - A - B;
    float resolution = NVR_PointShadowFocus.w;
    float2 q = (float2(dot(dir, A.xyz), dot(dir, B.xyz)) / (major * major) + 1.0f) * (0.5f * resolution) - 0.5f;
    float spacing = max(1.0f, 0.61f * s * resolution / major);
    float k = exp2(floor(log2(spacing)));
    float mix = spacing / k - 1.0f;
    float scale = 2.0f / resolution;
    float blocked = PointShadowTexelLevel(cube, q, k, M, A, B, scale, threshold);
#if POINT_SHADOW_FIT < 1
    [branch] if (mix > 0.001f) blocked = lerp(blocked, PointShadowTexelLevel(cube, q, 2.0f * k, M, A, B, scale, threshold), mix);
#endif
    return blocked;
}
#endif

float PointShadowCube(samplerCUBE cube, float4 light, float fade, float3 worldPos, float3 normal, float att) {
    [branch] if (!(light.w > 0.0f)) return 1.0f;
    float3 toPixel = worldPos - light.xyz;
    float len = length(toPixel);
    [branch] if (!(att > 0.0f) || !(len * light.w < 1.0f)) return 1.0f;

    float3 p = toPixel + normal * (NVR_PointShadowParams.z * len);
    float d = length(p) * light.w;
    float threshold = d - NVR_PointShadowParams.y * d;
#if POINT_SHADOW_MAG
    float nearK = NVR_PointShadowDebug.y > 0.0f ? 1.0f / (NVR_PointShadowDebug.y * light.w) : 1152921504606846976.0f;
#elif POINT_SHADOW_EVEN
    float nearK = 0.0f;
#endif

    float3 dir = p * float3(1.0f, 1.0f, -1.0f);

    float lit;
    [branch] if (NVR_PointShadowParams.w > 0.0f) {
        float major = max(abs(dir.x), max(abs(dir.y), abs(dir.z)));
        float s = NVR_PointShadowParams.w * major;
#if POINT_SHADOW_MAG
        s = max(s, saturate(len * rsqrt(dot(worldPos, worldPos)) * NVR_PointShadowFocus.x + NVR_PointShadowFocus.y) * NVR_PointShadowFocus.z * major);
#endif
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
            [branch] if (!(blockers > 0.0f)) return 1.0f;
            float blocker = (dot(search * isBlocker, 1.0f) + centre * centreBlocker) / blockers;
            float t = 1.0f / max(blocker, 0.0001f) - 1.0f / d;
            float size = fade < 0.0f ? NVR_PointShadowDebug.z : NVR_PointShadowSoft.z;
            s = clamp(t * (size * light.w * major + NVR_PointShadowSoft.x * s * d), NVR_PointShadowSoft.y * s, wide);
        }
#endif
#if POINT_SHADOW_TEXEL
        lit = 1.0f - PointShadowTexel(cube, dir, major, s, threshold, light.w);
#else
        float4 stored = float4(
            texCUBElod(cube, float4(dir + s * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
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
#if POINT_SHADOW_FIT == 1
#if POINT_SHADOW_EVEN
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
    return lerp(1.0f, lit, NVR_PointShadowParams.x * abs(fade));
}

float3 PointShadowFactor(float shadow, float4 tint) {
    float3 factor = shadow;
    [branch] if (NVR_PointShadowDebug.x > 0.0f) {
        factor = NVR_PointShadowDebug.x < 1.5f ? shadow + (1.0f - shadow) * tint.rgb
                                               : shadow * lerp(1.0f, tint.rgb, 0.6f);
    }
    return factor;
}

float3 PointShadowTrace(float3 worldPos, float3 normal, float valid) {
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
}

float PointShadow0(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube0, NVR_PointShadowLight[0], NVR_PointShadowTint[0].w, worldPos, normal, att); }
float PointShadow1(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube1, NVR_PointShadowLight[1], NVR_PointShadowTint[1].w, worldPos, normal, att); }
float PointShadow2(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube2, NVR_PointShadowLight[2], NVR_PointShadowTint[2].w, worldPos, normal, att); }
float PointShadow3(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube3, NVR_PointShadowLight[3], NVR_PointShadowTint[3].w, worldPos, normal, att); }
float PointShadow4(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube4, NVR_PointShadowLight[4], NVR_PointShadowTint[4].w, worldPos, normal, att); }
float PointShadow5(float3 worldPos, float3 normal, float att) { return PointShadowCube(NVR_PointShadowCube5, NVR_PointShadowLight[5], NVR_PointShadowTint[5].w, worldPos, normal, att); }

#ifdef POINT_SHADOW_SCALAR
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
