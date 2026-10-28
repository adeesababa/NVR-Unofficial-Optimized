#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_source;
layout(set = 1, binding = 1) uniform texture2D t_rendered;
layout(set = 1, binding = 2) uniform texture2D t_depth;
layout(set = 1, binding = 3) uniform texture2D t_normals;
layout(set = 1, binding = 4) uniform texture2D t_shadow;
layout(set = 1, binding = 5) uniform texture2D t_cookie;
layout(set = 1, binding = 6) uniform texture2D t_volumetric;

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 flashLightViewProj;
    mat4 viewTransform;
    mat4 projectionTransform;
    vec4 cameraPosition;
    vec4 cameraData;
    vec4 spotLightPosition;
    vec4 spotLightColor;
    vec4 spotLightDirection;
    vec4 sunColor;
    vec4 reciprocalResolution;
    vec4 flashLightTuning;
    vec4 flashLightComposite;
    vec4 volumetricControl;
} cb;

layout(push_constant) uniform Push {
    vec2 offsetMask;
    uint mode;
    uint linearSampler;
    uint cookieSampler;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearSampler)), (coord), 0.0)

#define FL_NEARFADE         cb.flashLightTuning.x
#define FL_SOFTEDGE         cb.flashLightTuning.y
#define FL_HOTSPOTLIMIT     cb.flashLightTuning.z
#define FL_COOKIESTRENGTH   cb.flashLightTuning.w
#define FL_LINEARSOURCE     cb.flashLightComposite.x
#define FL_EDGEBEAM         cb.flashLightComposite.y
#define FL_BEAMTEXEL        cb.flashLightComposite.zw

float shades(vec3 n, vec3 l) { return saturate(dot(n, l)); }
float invlerps(float a, float b, float t) { return saturate((t - a) / (b - a)); }
float sqr(float v) { return v * v; }
vec3 sqr(vec3 v) { return v * v; }

float readDepth(vec2 coord) {
    return LINEAR_LOD(t_depth, coord).x * cb.cameraData.y;
}
float readDepthLod(vec2 coord) {
    return LINEAR_LOD(t_depth, coord).x * cb.cameraData.y;
}

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

vec3 GetWorldNormal(vec2 coord) {
    vec4 normal = vec4(LINEAR_LOD(t_normals, coord).xyz * 2.0 - 1.0, 1.0);
    return (normal * cb.viewTransform).xyz;
}

vec4 ScreenCoordToTexCoord(vec4 coord) {
    coord.xyz /= coord.w;
    coord.x = coord.x * 0.5 + 0.5;
    coord.y = coord.y * -0.5 + 0.5;
    return coord;
}
