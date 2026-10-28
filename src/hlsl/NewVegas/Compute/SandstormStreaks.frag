#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_ortho;
layout(set = 1, binding = 1) uniform texture2D t_atlas;

#define SS_ORTHO
#define SS_SHADOWS
#include "SandstormCommon.glsl"

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 blockUV = (vpos + 0.5) * LowTexel;
    vec3 dir = normalize(toWorld(blockUV));
    float time = TESR_SandstormWind.w;
    vec2 wind = TESR_SandstormWind.xy;
    float speed = TESR_SandstormWind.z;
    vec3 camera = TESR_CameraPosition.xyz;

    vec3 grains = vec3(0.0);
    float sunlit = 0.0;
    if (TESR_SandstormShape.z > 0.0) {
        vec2 group = vpos - 2.0 * floor(vpos * 0.5);
        float jitter = fract((group.x + 2.0 * group.y + 0.5) / (4.0 * float(SANDFAST_STREAK_SAMPLES)) + time * 7.123);
        vec2 across = vec2(-wind.y, wind.x);
        const bool fine = TESR_SandstormQuality.w > 0.5;
        for (int s = 0; s < 3; s++) {
            float grain = 0.0;
            for (int m = 0; m < SANDFAST_STREAK_SAMPLES; m++) {
                float t = 50.0 + 120.0 * float(s) + 30.0 * fract(jitter + float(m) / float(SANDFAST_STREAK_SAMPLES));
                vec3 p = camera + dir * t - vec3(wind * speed * time * (2.5 + float(s)), 0.0);
                vec3 q = vec3(dot(p.xy, wind) / 240.0, dot(p.xy, across) / 8.0, p.z / 6.0);
                grain += smoothstep(0.74, 0.96, valueNoise(q + float(s) * 31.7));
            }
            grain *= (1.0 - float(s) * 0.25) / float(SANDFAST_STREAK_SAMPLES);
            float tMiddle = 65.0 + 120.0 * float(s);
            if (grain > 0.0) {
                grain *= InTheWindFast(dir * tMiddle, wind, fine);
                if (grain > 0.0) sunlit += grain * SunVisibility(dir * tMiddle);
            }
            grains[s] = grain;
        }
    }
    float total = grains.x + grains.y + grains.z;
    o_color = vec4(grains, total > 0.0 ? sunlit / total : 1.0);
}
