
layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 shadowNear;
    mat4 shadowMiddle;
    mat4 shadowFar;
    mat4 shadowLod;
    mat4 reproject;
    mat4 viewTransform;
    mat4 projectionTransform;
    vec4 shadowNearCenter;
    vec4 shadowMiddleCenter;
    vec4 shadowFarCenter;
    vec4 shadowLodCenter;
    vec4 shadowFormatData;
    vec4 shadowBlur;
    vec4 smoothedSunDir;
    vec4 sunColor;
    vec4 reciprocalResolution;
    vec4 cameraData;
    vec4 data;
    vec4 data2;
    vec4 data3;
    vec4 lightLayout;
    vec4 gridLayout;
    vec4 gridData;
    vec4 fogData;
    vec4 sunAmount;
    vec4 gameTime;
    vec4 fogDensity;
    vec4 fogShape;
    vec4 fogWind;
    vec4 fogScatter;
    vec4 fogWeather;
    vec4 fogNight;
    vec4 fogNightScatter;
    vec4 cameraPosition;
} cb;

layout(push_constant) uniform Push {
    uint pointIndex;
    uint linearIndex;
    uint wrapIndex;
    uint pad;
    vec2 axis;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define POINT_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointIndex)), (coord), 0.0)
#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearIndex)), (coord), 0.0)

#define farZ (cb.cameraData.y)
#define Strength (cb.data.x)
#define Darken (cb.data.y)
#define Density (cb.data.z)
#define MarchDistance (cb.data.w)
#define Steps (cb.data2.x)
#define Anisotropy (cb.data2.y)
#define Fade (cb.data2.z)
#define NoiseFrame (cb.data2.w)
#define HistoryWeight (cb.data3.x)
#define BlurRadius (cb.data3.y)
#define LowTexel (cb.lightLayout.xy)
#define GridSize (cb.gridLayout.xy)
#define GridAtlasTexel (cb.gridLayout.zw)

float hlslFmod(float x, float y) { return x - y * trunc(x / y); }

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

#ifdef VL_DEPTH
float readDepthLod(vec2 coord) { return POINT_LOD(t_depth, coord).x * farZ; }
#endif

#ifdef VL_SHADOWS
float Linstep(float a, float b, float v) { return saturate((v - a) / (b - a)); }
float ReduceLightBleeding(float pMax, float amount) { return Linstep(amount, 1.0, pMax); }

float ChebyshevUpperBound(vec2 moments, float mean, float minVariance, float lightBleedingReduction) {
    float variance = moments.y - (moments.x * moments.x);
    variance = max(variance, minVariance);
    float d = mean - moments.x;
    float pMax = variance / (variance + (d * d));
    pMax = ReduceLightBleeding(pMax, lightBleedingReduction);
    return (mean <= moments.x ? 1.0 : pMax);
}

float ShadowPCF2x2(vec2 coord, float depth, float texel) {
    vec2 p = coord / texel - 0.5;
    vec2 base = floor(p);
    vec2 f = p - base;
    vec2 uv0 = (base + 0.5) * texel;
    vec4 d = vec4(LINEAR_LOD(t_atlas, uv0).x, LINEAR_LOD(t_atlas, uv0 + vec2(texel, 0.0)).x,
                  LINEAR_LOD(t_atlas, uv0 + vec2(0.0, texel)).x, LINEAR_LOD(t_atlas, uv0 + vec2(texel)).x);
    vec4 lit = vec4(lessThanEqual(vec4(depth), d));
    return mix(mix(lit.x, lit.y, f.x), mix(lit.z, lit.w, f.x), f.y);
}

float GetLightAmountValueVSM(vec2 moments, float depth, float bias, float bleedReduction) {
    return ChebyshevUpperBound(moments, depth, bias, bleedReduction);
}

vec2 GetEVSMExponents(float positiveExponent, float negativeExponent, float formatBits) {
    float maxExponent = formatBits == 0.0 ? 5.54 : 42.0;
    vec2 lightSpaceExponents = vec2(positiveExponent, negativeExponent);
    return min(lightSpaceExponents, vec2(maxExponent));
}

vec2 WarpDepth(float depth, vec2 exponents) {
    depth = 2.0 * depth - 1.0;
    float pos = exp(exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
    return vec2(pos, neg);
}

float GetLightAmountValueEVSM2(vec2 moments, float depth, float bias, float bleedReduction, float formatBits) {
    vec2 exponents = GetEVSMExponents(40.0, 5.0, formatBits);
    vec2 warpedDepth = WarpDepth(depth, exponents);
    vec2 depthScale = bias * exponents * warpedDepth;
    vec2 minVariance = depthScale * depthScale;
    return ChebyshevUpperBound(moments, warpedDepth.x, minVariance.x, bleedReduction);
}

vec4 SunCascadeTexCoord(vec4 coord) {
    coord.xyz /= coord.w;
    coord.x = coord.x * 0.5 + 0.5;
    coord.y = coord.y * -0.5 + 0.5;
    return coord;
}

float MarchShadow(vec3 position) {
    mat4 lightTransform;
    vec2 quadrant;
    float bleed;
    if (length(position - cb.shadowNearCenter.xyz) < cb.shadowNearCenter.w) {
        lightTransform = cb.shadowNear; quadrant = vec2(0.0, 0.0); bleed = 0.1;
    }
    else if (length(position - cb.shadowMiddleCenter.xyz) < cb.shadowMiddleCenter.w) {
        lightTransform = cb.shadowMiddle; quadrant = vec2(0.5, 0.0); bleed = 0.2;
    }
    else if (length(position - cb.shadowFarCenter.xyz) < cb.shadowFarCenter.w) {
        lightTransform = cb.shadowFar; quadrant = vec2(0.0, 0.5); bleed = 0.6;
    }
    else if (length(position - cb.shadowLodCenter.xyz) < cb.shadowLodCenter.w) {
        lightTransform = cb.shadowLod; quadrant = vec2(0.5, 0.5); bleed = 0.8;
    }
    else return 1.0;
    vec4 lightSpace = SunCascadeTexCoord(lightTransform * vec4(position, 1.0));
    if (cb.shadowFormatData.x == 3.0) return ShadowPCF2x2(lightSpace.xy * 0.5 + quadrant, lightSpace.z - 0.0002, cb.shadowBlur.x);
    vec4 moments = LINEAR_LOD(t_atlas, lightSpace.xy * 0.5 + quadrant);
    float bias = cb.shadowFormatData.x == 0.0 ? 0.00001 : 0.01;
    if (cb.shadowFormatData.x == 0.0) return GetLightAmountValueVSM(moments.xy, lightSpace.z, bias, bleed);
    if (cb.shadowFormatData.x == 1.0) return GetLightAmountValueEVSM2(moments.xy, lightSpace.z, bias, bleed, cb.shadowFormatData.y);
    vec2 exponents = GetEVSMExponents(40.0, 5.0, cb.shadowFormatData.y);
    float warped = WarpDepth(lightSpace.z, exponents).x;
    float depthScale = bias * exponents.x * warped;
    return ChebyshevUpperBound(moments.xz, warped, depthScale * depthScale, bleed);
}
#endif

#ifdef VL_COMPOSITE
vec4 CompositeAir(vec2 coord, vec4 color, vec2 air) {
    float cosTheta = dot(normalize(toWorld(coord)), cb.smoothedSunDir.xyz);
    float g = Anisotropy;
    float phase = min((1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1.0e-4), 1.5), 8.0);

    vec3 lin = linearize(max(color.rgb, vec3(0.0)));
    float shadedAir = saturate(air.g - air.r);
    lin = lin * (1.0 - Darken * Fade * shadedAir) + linearize(cb.sunColor.rgb) * (Strength * Fade * phase * air.r);
    return vec4(delinearize(max(lin, vec3(0.0))), color.a);
}
#endif

#ifdef VL_GRID
float GridDepth(float s) { s /= 64.0; return MarchDistance * s * s; }

vec2 GridUV(float z, vec2 local) {
    vec2 tile = vec2(hlslFmod(z, 8.0), floor(z / 8.0));
    return (tile * GridSize + clamp(local, vec2(0.5), GridSize - 0.5)) * GridAtlasTexel;
}
#endif
