
layout(set = 1, binding = 17) uniform texture2D t_rtMarch;
layout(set = 1, binding = 18) uniform texture2D t_rtDir;
layout(set = 1, binding = 19) uniform texture2D t_rtRay;
layout(set = 1, binding = 20) uniform texture2D t_rtHit;
layout(set = 1, binding = 21) uniform texture2D t_rtShade;

layout(set = 2, binding = 1, std140) uniform RtConstants {
    vec4 rt;
    vec4 rt2;
    vec4 sunDir;
    vec4 sunColor;
} rtc;

#define RtOn (rtc.rt.x > 0.5)
#define RtMaxRoughness rtc.rt.y
#define RtDistance rtc.rt.z
#define RtDebugView rtc.rt.w
#define RtTolerance rtc.rt2.x

vec2 OctEncode(vec3 v) {
    v /= abs(v.x) + abs(v.y) + abs(v.z);
    vec2 e = v.xy;
    if (v.z < 0.0) e = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    return e;
}
vec3 OctDecode(vec2 e) {
    vec3 v = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    return normalize(v);
}

float RtFade(float roughness) { return 1.0 - smoothstep(0.75 * RtMaxRoughness, RtMaxRoughness, roughness); }

vec3 RtShadeHit(vec3 normal, vec3 albedo, float sunVisible) {
    vec3 light = CubeOn > 0.0 ? EnvAlong(normal, 1.0).rgb : SkyAlong(vec3(0.0, 0.0, 1.0)) * 0.5;
    float sunAmount = rtc.rt2.z;
    if (sunAmount > 0.0) light += linearize(rtc.sunColor.rgb) * saturate(dot(normal, normalize(rtc.sunDir.xyz))) * sunAmount * sunVisible;
    return albedo * light * rtc.rt2.y;
}
