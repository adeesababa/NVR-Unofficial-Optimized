
layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 TESR_ShadowCameraToLightTransformNear;
    mat4 TESR_ShadowCameraToLightTransformMiddle;
    mat4 TESR_ShadowCameraToLightTransformFar;
    mat4 TESR_ShadowCameraToLightTransformLod;
    mat4 TESR_ShadowCameraToLightTransformOrtho;
    mat4 TESR_ViewTransform;
    mat4 TESR_ProjectionTransform;
    vec4 TESR_ShadowNearCenter;
    vec4 TESR_ShadowMiddleCenter;
    vec4 TESR_ShadowFarCenter;
    vec4 TESR_ShadowLodCenter;
    vec4 TESR_ShadowFormatData;
    vec4 TESR_ShadowBlur;
    vec4 TESR_ShadowFade;
    vec4 TESR_CameraData;
    vec4 TESR_CameraPosition;
    vec4 TESR_ReciprocalResolution;
    vec4 TESR_SunPosition;
    vec4 TESR_SunAmount;
    vec4 TESR_SunColor;
    vec4 TESR_SunAmbient;
    vec4 TESR_FogColor;
    vec4 TESR_PBRData;
    vec4 TESR_SandstormData;
    vec4 TESR_SandstormWind;
    vec4 TESR_SandstormShape;
    vec4 TESR_SandstormColor;
    vec4 TESR_SandstormCover;
    vec4 TESR_SandstormLights;
    vec4 TESR_SandstormEmissive;
    vec4 TESR_SandstormWater;
    vec4 TESR_SandstormQuality;
    vec4 TESR_SandstormGridLayout;
    vec4 TESR_SandstormLowLayout;
    vec4 TESR_LightPosition[12];
    vec4 TESR_ShadowLightPosition[12];
    vec4 TESR_LightColor[24];
};

layout(push_constant) uniform Push {
    uint pointIndex;
    uint linearIndex;
    uint pad0;
    uint pad1;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define POINT_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointIndex)), (coord), 0.0)
#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearIndex)), (coord), 0.0)

#define farZ (TESR_CameraData.y)
#define GridSize (TESR_SandstormGridLayout.xy)
#define GridAtlasTexel (TESR_SandstormGridLayout.zw)
#define LowTexel (TESR_SandstormLowLayout.xy)
#define QuarterTexel (TESR_SandstormLowLayout.zw)
#define SANDFAST_STREAK_SAMPLES 4

float hlslFmod(float x, float y) { return x - y * trunc(x / y); }

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]);
    v += (1.0 / TESR_ProjectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(TESR_ViewTransform[0][0], TESR_ViewTransform[1][0], TESR_ViewTransform[2][0]);
    v += (-1.0 / TESR_ProjectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(TESR_ViewTransform[0][1], TESR_ViewTransform[1][1], TESR_ViewTransform[2][1]);
    return v;
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i), hash13(i + vec3(1.0, 0.0, 0.0)), f.x),
                   mix(hash13(i + vec3(0.0, 1.0, 0.0)), hash13(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0.0, 0.0, 1.0)), hash13(i + vec3(1.0, 0.0, 1.0)), f.x),
                   mix(hash13(i + vec3(0.0, 1.0, 1.0)), hash13(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float sum = valueNoise(p) * 0.55;
    float weight = 0.55;
    if (TESR_SandstormQuality.y > 1.5) {
        sum += valueNoise(p * 2.03 + 17.1) * 0.3;
        weight += 0.3;
        if (TESR_SandstormQuality.y > 2.5) {
            sum += valueNoise(p * 4.01 + 41.7) * 0.15;
            weight += 0.15;
        }
    }
    return sum / weight;
}

float phase(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

void DustLighting(vec3 dir, out vec3 dustAmbient, out vec3 dustSun) {
    float daytime = smoothstep(0.1, 0.6, TESR_SunAmount.x);
    vec3 ambient = (linearize(TESR_FogColor.rgb) * 0.6 + linearize(TESR_SunAmbient.rgb) * 0.4) * TESR_PBRData.w;
    vec3 sunLight = linearize(TESR_SunColor.rgb) * TESR_PBRData.z * daytime;
    float sunPhase = phase(dot(dir, normalize(TESR_SunPosition.xyz)), 0.55) * 4.0 * 3.14159265;
    dustAmbient = TESR_SandstormColor.rgb * ambient * TESR_SandstormColor.w;
    dustSun = TESR_SandstormColor.rgb * sunLight * mix(0.35, sunPhase, saturate(TESR_SandstormShape.w * 0.5)) * max(TESR_SandstormShape.w, 0.25)
        * TESR_SandstormColor.w;
}

float GridEnd() { return TESR_SandstormData.z * 1.5; }
float GridDistance(float s) { s /= 64.0; return GridEnd() * s * s; }
vec2 GridUV(float z, vec2 local) {
    vec2 tile = vec2(hlslFmod(z, 8.0), floor(z / 8.0));
    return (tile * GridSize + clamp(local, vec2(0.5), GridSize - 0.5)) * GridAtlasTexel;
}

vec4 Over(vec4 front, vec4 back) { return vec4(front.rgb + exp(-front.a) * back.rgb, front.a + back.a); }

vec3 PointGlow(vec4 light, vec4 color, vec3 camera, vec3 dir, float dist, float density) {
    if (light.w <= 0.0 || color.w <= 0.0) return vec3(0.0);
    vec3 toLight = light.xyz - camera;
    float along = dot(toLight, dir);
    float across = sqrt(max(dot(toLight, toLight) - along * along, 0.0) + light.w * light.w * 0.01);
    float swept = (atan((dist - along) / across) + atan(along / across)) / across;
    float through = exp(-density * clamp(along, 0.0, dist));
    return linearize(color.rgb) * color.w * light.w * light.w * swept * through;
}

vec3 PointGlowFast(vec4 light, vec4 color, vec3 camera, vec3 dir, float dist, float density) {
    if (light.w <= 0.0 || color.w <= 0.0) return vec3(0.0);
    vec3 toLight = light.xyz - camera;
    float along = dot(toLight, dir);
    float across2 = max(dot(toLight, toLight) - along * along, 0.0) + light.w * light.w * 0.01;
    float across = sqrt(across2);
    float swept = atan(dist * across, across2 - (dist - along) * along) / across;
    float through = exp(-density * clamp(along, 0.0, dist));
    return linearize(color.rgb) * color.w * light.w * light.w * swept * through;
}

const float LeeReach[8] = float[8](50.0, 110.0, 190.0, 290.0, 410.0, 550.0, 710.0, 900.0);

#ifdef SS_DEPTH
float readDepth(vec2 coord) { return POINT_LOD(t_depth, coord).x * farZ; }
float readDepthLod(vec2 coord) { return POINT_LOD(t_depth, coord).x * farZ; }

float ViewDistance(vec2 coord, float visibility) {
    float depth = readDepthLod(coord);
    if (depth / farZ >= 0.9961) return visibility * 6.0;
    return length(toWorld(coord) * depth);
}
#endif

#ifdef SS_SHADOWS
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

float SunCascadeShadow(vec3 position) {
    mat4 lightTransform;
    vec2 quadrant;
    float bleed;
    if (length(position - TESR_ShadowNearCenter.xyz) < TESR_ShadowNearCenter.w) {
        lightTransform = TESR_ShadowCameraToLightTransformNear; quadrant = vec2(0.0, 0.0); bleed = 0.1;
    }
    else if (length(position - TESR_ShadowMiddleCenter.xyz) < TESR_ShadowMiddleCenter.w) {
        lightTransform = TESR_ShadowCameraToLightTransformMiddle; quadrant = vec2(0.5, 0.0); bleed = 0.2;
    }
    else if (length(position - TESR_ShadowFarCenter.xyz) < TESR_ShadowFarCenter.w) {
        lightTransform = TESR_ShadowCameraToLightTransformFar; quadrant = vec2(0.0, 0.5); bleed = 0.6;
    }
    else if (length(position - TESR_ShadowLodCenter.xyz) < TESR_ShadowLodCenter.w) {
        lightTransform = TESR_ShadowCameraToLightTransformLod; quadrant = vec2(0.5, 0.5); bleed = 0.8;
    }
    else return 1.0;
    vec4 lightSpace = SunCascadeTexCoord(lightTransform * vec4(position, 1.0));
    if (TESR_ShadowFormatData.x == 3.0) return ShadowPCF2x2(lightSpace.xy * 0.5 + quadrant, lightSpace.z - 0.0002, TESR_ShadowBlur.x);
    vec4 moments = LINEAR_LOD(t_atlas, lightSpace.xy * 0.5 + quadrant);
    float bias = TESR_ShadowFormatData.x == 0.0 ? 0.00001 : 0.01;
    if (TESR_ShadowFormatData.x == 0.0) return GetLightAmountValueVSM(moments.xy, lightSpace.z, bias, bleed);
    if (TESR_ShadowFormatData.x == 1.0) return GetLightAmountValueEVSM2(moments.xy, lightSpace.z, bias, bleed, TESR_ShadowFormatData.y);
    vec2 exponents = GetEVSMExponents(40.0, 5.0, TESR_ShadowFormatData.y);
    float warped = WarpDepth(lightSpace.z, exponents).x;
    float depthScale = bias * exponents.x * warped;
    return ChebyshevUpperBound(moments.xz, warped, depthScale * depthScale, bleed);
}

float SunVisibility(vec3 relative) {
    if (TESR_ShadowFade.y < 0.5 || TESR_ShadowFade.x >= 0.999) return 1.0;
    return mix(saturate(SunCascadeShadow(relative)), 1.0, saturate(TESR_ShadowFade.x));
}
#endif

#ifdef SS_ORTHO
float OpenAbove(vec3 relative) {
    if (TESR_SandstormCover.x < 0.5) return 1.0;
    vec4 ortho = TESR_ShadowCameraToLightTransformOrtho * vec4(relative, 1.0);
    ortho.xyz /= ortho.w;
    if (abs(ortho.x) > 1.0 || abs(ortho.y) > 1.0 || ortho.z < 0.0 || ortho.z > 1.0) return 1.0;
    float top = POINT_LOD(t_ortho, vec2(ortho.x * 0.5 + 0.5, ortho.y * -0.5 + 0.5)).r;
    return top > ortho.z - 0.0001 ? 1.0 : 0.0;
}

float OpenAboveAt(vec4 ortho) {
    ortho.xyz /= ortho.w;
    float top = POINT_LOD(t_ortho, vec2(ortho.x * 0.5 + 0.5, ortho.y * -0.5 + 0.5)).r;
    bool outside = abs(ortho.x) > 1.0 || abs(ortho.y) > 1.0 || ortho.z < 0.0 || ortho.z > 1.0;
    return (outside || top > ortho.z - 0.0001) ? 1.0 : 0.0;
}

float InTheWind(vec3 relative, vec2 wind) {
    float reached = 1.0;
    for (int k = 0; k < 8; k++)
        reached *= OpenAbove(relative + vec3(-wind * LeeReach[k], LeeReach[k] * 0.2));
    return reached;
}

float InTheWindCoarse(vec3 relative, vec2 wind) {
    float reached = 1.0;
    for (int k = 1; k < 8; k += 2)
        reached *= OpenAbove(relative + vec3(-wind * LeeReach[k], LeeReach[k] * 0.2));
    return reached;
}

float InTheWindFast(vec3 relative, vec2 wind, bool fine) {
    if (TESR_SandstormCover.x < 0.5) return 1.0;
    vec4 base = TESR_ShadowCameraToLightTransformOrtho * vec4(relative, 1.0);
    vec4 stepVec = TESR_ShadowCameraToLightTransformOrtho * vec4(-wind, 0.2, 0.0);
    float reached = 1.0;
    if (fine) {
        for (int k = 0; k < 8; k++) reached *= OpenAboveAt(base + stepVec * LeeReach[k]);
    }
    else {
        for (int k = 1; k < 8; k += 2) reached *= OpenAboveAt(base + stepVec * LeeReach[k]);
    }
    return reached;
}
#endif

#ifdef SS_GRID
vec4 DustAt(vec2 where, float d) {
    float s = sqrt(saturate(d / GridEnd())) * 64.0;
    float z = min(floor(s), 63.0);
    vec2 local = where * GridSize;
    vec4 farSide = LINEAR_LOD(t_grid, GridUV(z, local));
    vec4 nearSide = z > 0.5 ? LINEAR_LOD(t_grid, GridUV(z - 1.0, local)) : vec4(0.0);
    return mix(nearSide, farSide, saturate(s - z));
}
#endif

#ifdef SS_GLOW
vec3 GlowAt(vec2 where, float depth01) {
    vec2 position = where / QuarterTexel - 0.5;
    vec2 base = floor(position);
    vec2 fracPos = position - base;
    float tolerance = max(depth01 * 0.04, 0.00002);
    vec3 glow = vec3(0.0);
    float weights = 1.0e-5;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            vec4 tap = POINT_LOD(t_glow, (base + vec2(float(x), float(y)) + 0.5) * QuarterTexel);
            float weight = (x == 1 ? fracPos.x : 1.0 - fracPos.x) * (y == 1 ? fracPos.y : 1.0 - fracPos.y);
            weight /= 1.0 + abs(tap.a - depth01) / tolerance;
            glow += tap.rgb * weight;
            weights += weight;
        }
    }
    return glow / weights;
}
#endif
