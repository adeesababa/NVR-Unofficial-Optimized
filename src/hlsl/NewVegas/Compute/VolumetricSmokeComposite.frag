#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_smoke;
layout(set = 1, binding = 3) uniform texture2D t_viewModel;
layout(set = 1, binding = 4) uniform texture2D t_low;
layout(set = 1, binding = 5) uniform texture2D t_haze;
layout(set = 1, binding = 6) uniform texture2D t_env;
layout(set = 1, binding = 7) uniform texture2D t_cut;

#define VS_DEPTH
#define VS_SMOKE
#include "VolumetricSmoke.glsl"

layout(location = 0) out vec4 o_color;

float CutAt(vec2 suv, float surface) {
    vec2 hres = ceil(1.0 / (4.0 * TESR_ReciprocalResolution.xy));
    vec2 p = suv * hres - 0.5;
    vec2 f = fract(p);
    vec2 at = (floor(p) + 0.5) / hres, stepSize = 1.0 / hres;
    vec4 c00 = POINT_LOD(t_cut, at);
    vec4 c10 = POINT_LOD(t_cut, at + vec2(stepSize.x, 0.0));
    vec4 c01 = POINT_LOD(t_cut, at + vec2(0.0, stepSize.y));
    vec4 c11 = POINT_LOD(t_cut, at + stepSize);
    vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    vec4 off = abs(vec4(c00.a, c10.a, c01.a, c11.a) - surface) / (2.0 + 0.04 * surface);
    w *= 1.0 / (1.0 + off * off * off) + 0.0001;
    return dot(vec4(c00.x, c10.x, c01.x, c11.x), w) / dot(w, vec4(1.0));
}

vec4 HazeAt(vec2 suv, float surface) {
    vec2 hres = ceil(1.0 / (4.0 * TESR_ReciprocalResolution.xy));
    vec2 p = suv * hres - 0.5;
    vec2 f = fract(p);
    vec2 at = (floor(p) + 0.5) / hres, stepSize = 1.0 / hres;
    vec4 h00 = LINEAR_LOD(t_haze, at);
    vec4 h10 = LINEAR_LOD(t_haze, at + vec2(stepSize.x, 0.0));
    vec4 h01 = LINEAR_LOD(t_haze, at + vec2(0.0, stepSize.y));
    vec4 h11 = LINEAR_LOD(t_haze, at + stepSize);
    vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    vec4 off = abs(vec4(h00.a, h10.a, h01.a, h11.a) - surface) / (2.0 + 0.04 * surface);
    w *= 1.0 / (1.0 + off * off * off) + 0.0001;
    return (h00 * w.x + h10 * w.y + h01 * w.z + h11 * w.w) / dot(w, vec4(1.0));
}

vec4 CompositePS() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy * TESR_ReciprocalResolution.xy;
    vec4 color = POINT_LOD(t_rendered, uv);
    vec2 suv = (vpos + 0.5) * TESR_ReciprocalResolution.xy;
    vec4 smokeBuffer = SmokeAt(suv);
    vec4 haze = LINEAR_LOD(t_haze, suv);
    float tau = smokeBuffer.r + haze.r;
    if (tau < 0.0005) return color;
    if (SmokeInfo.w > 0.5 && SmokeInfo.y < 0.5 && POINT_LOD(t_viewModel, uv).x > 0.0) return color;
    float transmittance = exp(-tau);
    if (SmokeInfo.y > 0.5) {
        color.rgb = mix(color.rgb, vec3(1.0, 0.05, 0.05), saturate(1.0 - exp(-4.0 * tau)));
        return color;
    }
    float viewDepth;
    vec3 world = reconstructWorldPosition(uv, viewDepth).xyz;
    vec3 camera = (TESR_InvViewTransform * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float surface = length(world - camera);
    haze = HazeAt(suv, surface);
    float smokeDist = smokeBuffer.a / max(smokeBuffer.r, 0.000001);
    if (smokeBuffer.r >= 0.0005 && HazeInfo.y > 0.5) {
        float cut = CutAt(suv, surface);
        cut *= 1.0 - saturate(BulletMove.x) * (1.0 - smoothstep(60.0, 150.0, smokeDist));
        if (cut > 0.0) {
            float seen = mix(exp(-smokeBuffer.r), 1.0, cut);
            smokeBuffer *= saturate(-log(max(seen, 0.000001)) / max(smokeBuffer.r, 0.000001));
        }
    }
    if (SmokeLook.w > 0.5) smokeBuffer *= saturate((surface - smokeDist) / max(4.0, 0.08 * smokeDist) + 0.5);
    float tauSmoke = smokeBuffer.r, tauHaze = haze.r;
    tau = tauSmoke + tauHaze;
    if (tau < 0.0005) return color;
    transmittance = exp(-tau);
    const float age = 0.35;
    vec3 env = POINT_LOD(t_env, vec2(0.5, 0.5)).rgb;
    vec3 dir = normalize(world - camera);
    float cosView = dot(dir, LightDirection());
    float forward = 0.064 / pow(1.36 - 1.2 * cosView, 1.5);
    float outdoors = SmokeInfo.z;
    vec3 ambient = mix(env, 0.72 * TESR_SunAmbient.rgb, 0.5 * outdoors) * mix(0.75, 0.8, outdoors);
    vec3 direct = outdoors > 0.5 ? 0.5 * TESR_SunColor.rgb * (0.4 + 1.1 * forward) : 0.45 * env;
    float deepSmoke = 1.0 - 0.5 * SmokeParams.z * (1.0 - exp(-tauSmoke));
    float deepHaze = 1.0 - 0.5 * SmokeParams.z * (1.0 - exp(-tauHaze));
    vec3 smokeLight = ambient * deepSmoke + direct * saturate(smokeBuffer.g / max(tauSmoke, 0.000001)) +
        SmokeFlashColor.rgb * (smokeBuffer.b / max(tauSmoke, 0.000001));
    vec3 hazeLight = ambient * deepHaze + direct * saturate(haze.g / max(tauHaze, 0.000001)) +
        SmokeFlashColor.rgb * (haze.b / max(tauHaze, 0.000001));
    if (SmokeLook.y > 0.001 && smokeBuffer.r >= 0.0005) {
        vec2 px = TESR_ReciprocalResolution.xy * 2.0;
        float hL = 1.0 - exp(-SmokeAtLod(suv - vec2(px.x, 0.0)).r);
        float hR = 1.0 - exp(-SmokeAtLod(suv + vec2(px.x, 0.0)).r);
        float hU = 1.0 - exp(-SmokeAtLod(suv - vec2(0.0, px.y)).r);
        float hD = 1.0 - exp(-SmokeAtLod(suv + vec2(0.0, px.y)).r);
        vec3 bump = normalize(vec3(hL - hR, hD - hU, 0.3));
        vec3 lightView = LightDirection() * mat3(TESR_InvViewTransform);
        vec3 lightScreen = normalize(vec3(lightView.x, lightView.y, 0.3 - lightView.z));
        float relief = (dot(bump, lightScreen) * 0.5 + 0.5) / max(lightScreen.z * 0.5 + 0.5, 0.2);
        smokeLight *= mix(1.0, clamp(relief, 0.8, 1.25), SmokeLook.y);
    }
    vec3 smoke = (smokeLight * tauSmoke + hazeLight * tauHaze) / tau * SmokeParams.y;
    float tint = SmokeLook.x;
    float thin = exp(-1.5 * tau);
    smoke *= mix(vec3(1.0), vec3(0.86, 0.93, 1.06), thin * tint) * mix(1.0, 1.12 - 0.24 * age, tint);
    vec3 through = exp(-tau * mix(vec3(1.0), vec3(0.93, 1.0, 1.08), tint));
    if (SmokeInfo.x < -0.5) { smoke = vec3(0.0, 1.0, 0.0); through = vec3(exp(-tau)); }
    color.rgb = color.rgb * through + smoke * (1.0 - through);
    return color;
}

void main() {
    o_color = CompositePS();
}
