#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 projectionTransform;
    mat4 invProjectionTransform;
    mat4 invViewTransform;
    mat4 shadowCameraToLightOrtho;
    vec4 aoData;
    vec4 data;
    vec4 gtao;
    vec4 sky;
    vec4 sky2;
    vec4 sky3;
    vec4 sky4;
    vec4 sky5;
    vec4 smoothedSunDir;
    vec4 shadowFade;
    vec4 reciprocalResolution;
    vec4 fogData;
    vec4 fogColor;
    vec4 depthConstants;
    vec4 cameraData;
    vec4 aoLayout;
    vec4 skyLayout;
    mat4 rtReproject;
    vec4 rtTemporal;
} cb;

layout(push_constant) uniform Push {
    vec2 axis;
    uint gtaoEstimate;
    uint linearSampler;
    uint pointSampler;
    uint pointWrapSampler;
} pc;

#ifdef AO_GTAO
#define AO_GTAO_ESTIMATE (AO_GTAO != 0)
#else
#define AO_GTAO_ESTIMATE (pc.gtaoEstimate != 0u)
#endif

#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearSampler)), (coord), 0.0)
#define POINT_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointSampler)), (coord), 0.0)
#define POINT_WRAP_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointWrapSampler)), (coord), 0.0)

#define expand(v) (((v) - 0.5) / 0.5)
#define compress(v) (((v) * 0.5) + 0.5)
#define invlerp(a, b, t) ((t - a) / (b - a))
const float PI = 3.1415926538;

float acosHlsl(float x) {
    float a = abs(x);
    float r = ((-0.0187292993 * a + 0.0742610022) * a - 0.212114394) * a + 1.57072878;
    r *= sqrt(abs(1.0 - a));
    return x < 0.0 ? 3.14159274 - r : r;
}

#define AOstrength (cb.aoData.y)
#define AOclamp (cb.aoData.z)
#define AOrange (cb.aoData.w)
#define AOangleBias (cb.data.x)
#define AOlumThreshold (cb.data.y)
#define blurDrop (cb.data.z)
#define blurRadius (cb.data.w)
const float startFade = 2000.0;
const float endFade = 8000.0;
const int kernelSize = 5;

#define invertedDepth (cb.depthConstants.z != 0.0)
#define nearZ (cb.cameraData.x)
#define farZ (cb.cameraData.y)

float projectedDepthFromLinear(float linear01) {
    float viewZ = max(linear01 * farZ, 1.0e-6);
    return nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ));
}

vec3 projectPosition(vec3 position) {
    vec4 projection = cb.projectionTransform * vec4(position, 1.0);
    projection.xyz /= projection.w;

    projection.x = projection.x * 0.5 + 0.5;
    projection.y = 0.5 - 0.5 * projection.y;

    return projection.xyz;
}

const int cKernelSize = 12;
const float BlurWeights[cKernelSize] = float[](
    0.057424882, 0.058107773, 0.061460144, 0.071020611, 0.088092873, 0.106530916,
    0.106530916, 0.088092873, 0.071020611, 0.061460144, 0.058107773, 0.057424882);
const float BlurSteps[cKernelSize] = float[](-6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
#define BlurOffsets(i) (vec2(BlurSteps[i] * cb.reciprocalResolution.x, BlurSteps[i] * cb.reciprocalResolution.y))

vec2 DedicatedUV(vec2 uv) {
    return clamp(uv, 0.5 * cb.aoLayout.zw, 1.0 - 0.5 * cb.aoLayout.zw);
}

float SkyShare(vec3 worldNormal, float sunMask) {
    float ambientE = cb.sky2.z;
    float sunE = cb.sky2.w * saturate(dot(worldNormal, cb.smoothedSunDir.xyz)) * sunMask;
    return ambientE / max(ambientE + sunE, 1.0e-5);
}

float SkyFactor(float visibility, vec3 worldNormal, float sunMask, float covered) {
    float share = cb.sky3.z > 0.5 ? SkyShare(worldNormal, sunMask) : 1.0;
    float factor = 1.0 - saturate(cb.sky2.y * (1.0 - visibility) * share);
    float view = cb.sky3.w;
    if (view > 0.5) factor = view < 1.5 ? visibility : (view < 2.5 ? share : (view < 3.5 ? factor : 1.0 - covered));
    return factor;
}

vec3 SkyTake(vec3 linearColor, float factor, float visibility, float cap, float noLimit) {
    float take = min(1.0 - factor, ((1.0 - visibility) * cap + noLimit) / max(luma(linearColor), 1.0e-6));
    return linearColor * (1.0 - take);
}
