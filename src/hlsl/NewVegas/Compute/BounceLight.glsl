#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 projectionTransform;
    mat4 invProjectionTransform;
    mat4 bounceReproject;
    vec4 bounceLightData;
    vec4 reciprocalResolution;
    vec4 bounceLayout;
    vec4 bounceTemporal;
    vec4 bounceAmbient;
    vec4 cameraData;
} cb;

layout(push_constant) uniform Push {
    uint pointSampler;
    uint linearSampler;
    uint wrapSampler;
    uint pad;
    vec4 pass_;
} pc;

#define POINT_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointSampler)), (coord), 0.0)
#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearSampler)), (coord), 0.0)
#define WRAP_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.wrapSampler)), (coord), 0.0)

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

#define Strength (cb.bounceLightData.x)
#define Radius (max(cb.bounceLightData.y, 1.0))
#define DrawDistance (cb.bounceLightData.z)
#define BlurRadius (cb.bounceLightData.w)
const float NearFade = 40.0;
const float MaxScreenRadius = 0.06;

float smoothstepHlsl(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3.0 - 2.0 * t);
}
float stepHlsl(float y, float x) { return x >= y ? 1.0 : 0.0; }

vec3 PositionFromLinear(vec2 uv, float linear01) {
    float z = projectedDepthFromLinear(linear01);
    vec4 viewSpace = cb.invProjectionTransform * vec4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, z, 1.0);
    return viewSpace.xyz / viewSpace.w;
}

float Pack2(vec2 v) {
    v = floor(clamp(v, 0.0, 1.0) * 4095.0 + 0.5);
    return v.x * 4096.0 + v.y;
}

vec2 Unpack2(float p) {
    float high = floor(p / 4096.0);
    return vec2(high, p - high * 4096.0) / 4095.0;
}

vec2 OctEncode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * s;
    return e * 0.5 + 0.5;
}

vec3 OctDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

vec3 LastFrame(vec3 position) {
    vec4 clip = cb.bounceReproject * vec4(position, 1.0);
    float w = max(clip.w, 0.001);
    return vec3(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5, clip.w);
}

float DepthTolerance(vec3 position, vec3 normal) {
    float facing = max(abs(dot(normal, normalize(position))), 0.05);
    float texel = 2.0 * cb.bounceLayout.z / cb.projectionTransform[0][0];
    return position.z * (0.03 + 2.0 * texel * sqrt(1.0 - facing * facing) / facing) + 4.0;
}

float HistoryAt(texture2D historyTex, vec3 last, float tolerance, out vec3 history) {
    vec2 position = last.xy / cb.bounceLayout.zw - 0.5;
    vec2 base = floor(position);
    vec2 f = position - base;
    vec4 t00 = POINT_LOD(historyTex, (base + 0.5) * cb.bounceLayout.zw);
    vec4 t10 = POINT_LOD(historyTex, (base + vec2(1.5, 0.5)) * cb.bounceLayout.zw);
    vec4 t01 = POINT_LOD(historyTex, (base + vec2(0.5, 1.5)) * cb.bounceLayout.zw);
    vec4 t11 = POINT_LOD(historyTex, (base + 1.5) * cb.bounceLayout.zw);
    vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    w *= vec4(lessThan(abs(vec4(t00.a, t10.a, t01.a, t11.a) - last.z), vec4(tolerance)));
    float sum = dot(w, vec4(1.0));
    history = (t00.rgb * w.x + t10.rgb * w.y + t01.rgb * w.z + t11.rgb * w.w) / max(sum, 1.0e-4);
    bool inside = all(greaterThan(last.xy, vec2(0.0))) && all(lessThan(last.xy, vec2(1.0))) && last.z > NearFade;
    return inside ? sum : 0.0;
}
