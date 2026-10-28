#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_depth;
layout(set = 1, binding = 1) uniform texture2D t_atlas;
layout(set = 1, binding = 2) uniform texture2D t_normals;
layout(set = 1, binding = 3) uniform texture2D t_shadow;
layout(set = 1, binding = 4) uniform texture2D t_noise;
layout(set = 1, binding = 5) uniform texture2D t_charShadow;

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 cascadeNear;
    mat4 cascadeMiddle;
    mat4 cascadeFar;
    mat4 cascadeLod;
    mat4 projectionTransform;
    mat4 invProjectionTransform;
    mat4 viewTransform;
    mat4 invViewTransform;
    vec4 reciprocalResolution;
    vec4 smoothedSunDir;
    vec4 viewSpaceLightDir;
    vec4 shadowFormatData;
    vec4 screenSpaceData;
    vec4 shadowFade;
    vec4 shadowBlur;
    vec4 forwardData;
    vec4 nearCenter;
    vec4 middleCenter;
    vec4 farCenter;
    vec4 lodCenter;
    vec4 cameraData;
    vec4 flags;
    mat4 charShadowCameraToLight;
    vec4 charShadowCenter;
    vec4 charShadowData;
} cb;

layout(push_constant) uniform Push {
    uint linearIndex;
    uint noiseIndex;
    uint pass;
    uint pad;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define LINEAR_S NVR_SAMPLER(pc.linearIndex)

#define SSS_STEPNUM 5
const float SSS_DIST = 2000.0;
const float SSS_THICKNESS = 20.0;
#define SSS_MAXDEPTH (cb.screenSpaceData.z * cb.screenSpaceData.x)
#define Mode cb.shadowFormatData.x
#define FormatBits cb.shadowFormatData.y
const float NormalBiasTexels = 2.5;
const float SlopeBias = 1.0;

#define nearZ cb.cameraData.x
#define farZ cb.cameraData.y

float hlslSmoothstep(float a, float b, float x);
float ContactNearFade(float z) { return cb.flags.z > 0.0 ? hlslSmoothstep(cb.flags.y, cb.flags.z, z) : 1.0; }
float hlslSmoothstep(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3.0 - 2.0 * t);
}

vec4 sampleShadow(vec2 coord) { return textureLod(sampler2D(t_shadow, LINEAR_S), coord, 0.0); }

float projectedDepthFromLinear(float linear01) {
    float viewZ = max(linear01 * farZ, 1.0e-6);
    return nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ));
}

float readDepth(vec2 coord) {
    return textureLod(sampler2D(t_depth, LINEAR_S), coord, 0.0).x * farZ;
}

float readDepthLod(vec2 coord) {
    return textureLod(sampler2D(t_depth, LINEAR_S), coord, 0.0).x * farZ;
}

vec3 reconstructPosition(vec2 coord) {
    float x = coord.x * 2.0 - 1.0;
    float y = (1.0 - coord.y) * 2.0 - 1.0;
    float z = projectedDepthFromLinear(textureLod(sampler2D(t_depth, LINEAR_S), coord, 0.0).x);
    vec4 clipSpace = vec4(x, y, z, 1.0);

    vec4 viewSpace = cb.invProjectionTransform * clipSpace;

    viewSpace /= viewSpace.w;

    return viewSpace.xyz;
}

vec3 projectPosition(vec3 position) {
    vec4 projection = cb.projectionTransform * vec4(position, 1.0);
    projection.xyz /= projection.w;

    projection.x = projection.x * 0.5 + 0.5;
    projection.y = 0.5 - 0.5 * projection.y;

    return projection.xyz;
}

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += vec3(1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += vec3(-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

float getHomogenousDepth(vec2 coord) {
    float depth = readDepth(coord);
    vec3 camera_vector = toWorld(coord) * depth;
    return length(camera_vector);
}

vec4 reconstructWorldPositionLod(vec2 coord, out float viewDepth) {
    float x = coord.x * 2.0 - 1.0;
    float y = (1.0 - coord.y) * 2.0 - 1.0;
    float z = projectedDepthFromLinear(textureLod(sampler2D(t_depth, LINEAR_S), coord, 0.0).x);
    vec4 viewSpace = cb.invProjectionTransform * vec4(x, y, z, 1.0);

    viewSpace /= viewSpace.w;
    viewDepth = viewSpace.z;

    vec4 worldSpace = cb.invViewTransform * viewSpace;
    return vec4(worldSpace.xyz, 1.0);
}

vec3 GetNormal(vec2 coord) {
    return textureLod(sampler2D(t_normals, LINEAR_S), coord, 0.0).xyz * 2.0 - 1.0;
}

vec3 GetWorldNormalLod(vec2 coord) {
    vec4 normal = vec4(textureLod(sampler2D(t_normals, LINEAR_S), coord, 0.0).xyz * 2.0 - 1.0, 1.0);
    return (normal * cb.viewTransform).xyz;
}

const int cKernelSize = 12;

const float BlurWeights[cKernelSize] = float[cKernelSize](
    0.057424882,
    0.058107773,
    0.061460144,
    0.071020611,
    0.088092873,
    0.106530916,
    0.106530916,
    0.088092873,
    0.071020611,
    0.061460144,
    0.058107773,
    0.057424882
);

vec2 BlurOffsets(int i) {
    float k = i < 6 ? float(i - 6) : float(i - 5);
    return vec2(k * cb.reciprocalResolution.x, k * cb.reciprocalResolution.y);
}

vec4 DepthBlurKeep(vec2 coord, vec2 OffsetMask, float blurRadius, float depthDrop, float endFade) {
    vec4 center = sampleShadow(coord);
    float depth1 = readDepthLod(coord);
    if (depth1 > endFade) return center;

    vec4 taps[cKernelSize];
    vec4 spread = vec4(0.0);
    for (int t = 0; t < cKernelSize; t++) {
        taps[t] = sampleShadow(coord + (BlurOffsets(t) * OffsetMask) * blurRadius);
        spread = max(spread, abs(taps[t] - center));
    }
    if (max(spread.r, spread.g) < 0.0001) return vec4(center.rgb, 1.0);

    float WeightSum = 0.114725602;
    vec4 color1 = center * WeightSum;
    depthDrop *= (depth1 / farZ);

    for (int i = 0; i < cKernelSize; i++) {
        float depth2 = readDepthLod(coord + (BlurOffsets(i) * OffsetMask) * blurRadius);
        float diff = abs(depth1 - depth2);

        float useForBlur = (diff <= depthDrop) ? 1.0 : 0.0;
        color1 += BlurWeights[i] * taps[i] * useForBlur;
        WeightSum += BlurWeights[i] * useForBlur;
    }
    color1 /= WeightSum;
    return vec4(color1.rgb, 1.0);
}
