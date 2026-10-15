#ifndef INTERIOR_SHADOWS
    #define INTERIOR_SHADOWS 0
#endif

#if INTERIOR_SHADOWS

#ifndef POINT_SHADOW_SMOOTH
    #define POINT_SHADOW_SMOOTH 0
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

float PointShadowCube(samplerCUBE cube, float4 light, float fade, float3 worldPos, float3 normal, float att) {
    [branch] if (!(light.w > 0.0f)) return 1.0f;
    float3 toPixel = worldPos - light.xyz;
    float len = length(toPixel);
    [branch] if (!(att > 0.0f) || !(len * light.w < 1.0f)) return 1.0f;

    float3 p = toPixel + normal * (NVR_PointShadowParams.z * len);
    float d = length(p) * light.w;
    float threshold = d - NVR_PointShadowParams.y * d;

    float3 dir = p * float3(1.0f, 1.0f, -1.0f);

    float lit;
    [branch] if (NVR_PointShadowParams.w > 0.0f) {
        float s = NVR_PointShadowParams.w * max(abs(dir.x), max(abs(dir.y), abs(dir.z)));
        [branch] if (NVR_PointShadowSoft.x > 0.0f) {
            float4 search = float4(
                texCUBElod(cube, float4(dir + s * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
                texCUBElod(cube, float4(dir + s * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
                texCUBElod(cube, float4(dir + s * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
                texCUBElod(cube, float4(dir + s * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
            float centre = texCUBElod(cube, float4(dir, 0.0f)).r;
            float4 isBlocker = (float4)(search <= threshold) * (float4)(search > 0.0f);
            float centreBlocker = (float)(centre <= threshold) * (float)(centre > 0.0f);
            float blockers = dot(isBlocker, 1.0f) + centreBlocker;
            [branch] if (!(blockers > 0.0f)) return 1.0f;
            float blocker = (dot(search * isBlocker, 1.0f) + centre * centreBlocker) / blockers;
            s *= clamp((d - blocker) / max(blocker, 0.0001f) * NVR_PointShadowSoft.x, NVR_PointShadowSoft.y, 1.0f);
        }
        float4 stored = float4(
            texCUBElod(cube, float4(dir + s * float3( 1.0f,  1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f, -1.0f,  1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3( 1.0f, -1.0f, -1.0f), 0.0f)).r,
            texCUBElod(cube, float4(dir + s * float3(-1.0f,  1.0f, -1.0f), 0.0f)).r);
#if POINT_SHADOW_SMOOTH
        float invRamp = 1.0f / max(threshold - min(min(stored.x, stored.y), min(stored.z, stored.w)), 0.0001f);
        float4 blocked = saturate((threshold - stored) * invRamp) * (float4)(stored > 0.0f) * PointShadowNear(stored, light.w);
#else
        float4 blocked = (float4)(stored <= threshold) * (float4)(stored > 0.0f) * PointShadowNear(stored, light.w);
#endif
        [branch] if (NVR_PointShadowDebug.w > 1.5f) {
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
        }
        else lit = 1.0f - dot(blocked, 0.25f);
    }
    else {
        float stored = texCUBElod(cube, float4(dir, 0.0f)).r;
        lit = 1.0f - (float)(stored <= threshold) * (float)(stored > 0.0f) * PointShadowNear(stored, light.w).x;
    }
    return lerp(1.0f, lit, NVR_PointShadowParams.x * fade);
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
