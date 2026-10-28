#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 projectionTransform;
    mat4 invProjectionTransform;
    vec4 reciprocalResolution;
    vec4 sunColor;
    vec4 godRaysRay;
    vec4 godRaysRayColor;
    vec4 godRaysData;
    vec4 viewSpaceLightDir;
    vec4 sunDirection;
    vec4 sunPosition;
    vec4 sunAmount;
    vec4 sunsetColor;
    vec4 cameraData;
    vec4 layout_;
} cb;

layout(push_constant) uniform Push {
    vec4 skip;
    vec4 reduce;
    float blurStep;
    float storeAttenuation;
    uint linearSampler;
    uint pointSampler;
} pc;

#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearSampler)), (coord), 0.0)
#define POINT_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointSampler)), (coord), 0.0)

#define compress(v) (((v) * 0.5) + 0.5)
#define shade(n, l) max(dot(n, l), 0.0)
const vec4 blue = vec4(0.0, 0.0, 1.0, 1.0);

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

#define raspect (1.0 / cb.reciprocalResolution.z)
const float samples = 10.0;
#define lumTreshold (cb.godRaysData.y)
#define multiplier (cb.godRaysData.z)
#define intensity (cb.godRaysRay.x)
#define stepLengthMult (cb.godRaysRay.y)
#define sunHeight (1.0 - shade(cb.sunPosition.xyz, blue.xyz))

float smoothstepHlsl(float edge0, float edge1, float x) {
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}
