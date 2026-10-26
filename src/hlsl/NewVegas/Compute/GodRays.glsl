// Shared by the GodRays Vulkan passes (needs NVR DXVK): the constants and helpers of Effects/GodRays.fx.hlsl (DedicatedGodRays and
// GodRaysMaxReduce) and its includes (Depth.hlsl, Sky.hlsl, Helpers.hlsl), line for line. effects/GodRays.cpp RenderDedicated.
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

// The constants as the D3D9 constant table holds them this frame, uploaded once per frame (too big for push data). Matrices: the
// D3DX matrix as stored (SetMatrix keeps the logical matrix), so cb.m[i][j] is the HLSL's m[i][j] and mul(v, M) is cb.m * v.
layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 projectionTransform;      // TESR_ProjectionTransform
    mat4 invProjectionTransform;   // TESR_InvProjectionTransform
    vec4 reciprocalResolution;     // TESR_ReciprocalResolution
    vec4 sunColor;                 // TESR_SunColor
    vec4 godRaysRay;               // TESR_GodRaysRay: x intensity, y length, z density, w visibility
    vec4 godRaysRayColor;          // TESR_GodRaysRayColor: rgb, w saturate
    vec4 godRaysData;              // TESR_GodRaysData: x passes, y luminance, z multiplier, w time enabled
    vec4 viewSpaceLightDir;        // TESR_ViewSpaceLightDir
    vec4 sunDirection;             // TESR_SunDirection
    vec4 sunPosition;              // TESR_SunPosition
    vec4 sunAmount;                // TESR_SunAmount
    vec4 sunsetColor;              // TESR_SunsetColor
    vec4 cameraData;               // TESR_CameraData: x near, y far
    vec4 layout_;                  // NVR_GodRaysLayout: xy half extent / full extent, zw 1 / half-resolution dimensions
} cb;

// Per pass (48 bytes).
layout(push_constant) uniform Push {
    vec4 skip;                     // 0: NVR_GodRaysSkip (x SkipEmpty reduction ran, y SkipIdle on, z SkipIdle's limit)
    vec4 reduce;                   // 16: NVR_GodRaysReduce (xy 1 / this reduction step's source size)
    float blurStep;                // 32: RadialBlur's uniform step
    float storeAttenuation;        // 36: RadialBlur's uniform storeAttenuation (0 or 1)
    uint linearSampler;            // 40: LINEAR, CLAMP (TESR_ samplers, NVR_GodRaysBuffer)
    uint pointSampler;             // 44: POINT, CLAMP (NVR_GodRaysMax)
} pc;

#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearSampler)), (coord), 0.0)
#define POINT_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointSampler)), (coord), 0.0)

// Helpers.hlsl
#define compress(v) (((v) * 0.5) + 0.5)
#define shade(n, l) max(dot(n, l), 0.0)
const vec4 blue = vec4(0.0, 0.0, 1.0, 1.0);

// Depth.hlsl
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

// GodRays.fx.hlsl statics
#define raspect (1.0 / cb.reciprocalResolution.z)
const float samples = 10.0;
#define lumTreshold (cb.godRaysData.y)
#define multiplier (cb.godRaysData.z)
#define intensity (cb.godRaysRay.x)
#define stepLengthMult (cb.godRaysRay.y)
#define sunHeight (1.0 - shade(cb.sunPosition.xyz, blue.xyz))

// HLSL smoothstep (GLSL's is undefined for edge0 >= edge1)
float smoothstepHlsl(float edge0, float edge1, float x) {
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}
