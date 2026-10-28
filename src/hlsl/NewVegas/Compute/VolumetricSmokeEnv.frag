#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;

#include "VolumetricSmoke.glsl"

layout(location = 0) out vec4 o_env;

void main() {
    vec3 env = vec3(0.0);
    for (int gy = 0; gy < 8; gy++)
        for (int gx = 0; gx < 8; gx++)
            env += POINT_LOD(t_rendered, vec2(0.0625 + 0.125 * float(gx), 0.0625 + 0.125 * float(gy))).rgb;
    env = 1.8 * env / 64.0 + 0.05;
    env = mix(vec3(dot(env, vec3(0.299, 0.587, 0.114))), env, 0.5);
    o_env = vec4(env, saturate(EnvInfo.x));
}
