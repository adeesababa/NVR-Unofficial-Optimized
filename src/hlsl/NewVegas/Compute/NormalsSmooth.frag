#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_source;
layout(set = 1, binding = 1) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;
    vec4 smoothData;
    vec4 cameraData;
    mat4 projection;
    uint samplerIndex;
    uint alongY;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_normal;

vec3 expandNormal(vec3 v) { return (v - 0.5) / 0.5; }
vec3 compressNormal(vec3 v) { return (v * 0.5) + 0.5; }

vec3 positionFromViewZ(vec2 p, float z) {
    return vec3((p.x * 2.0 - 1.0) / pc.projection[0][0], (1.0 - p.y * 2.0) / pc.projection[1][1], 1.0) * z;
}

void main() {
    bool alongY = pc.alongY != 0u;
    float farZ = pc.cameraData.y;

    vec4 source = textureLod(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    vec3 normal = normalize(expandNormal(source.xyz));
    float viewZ = textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0).x * farZ;

    if (pc.smoothData.x > 0.5 && viewZ < farZ * 0.99) {
        vec3 origin = positionFromViewZ(uv, viewZ);
        vec2 axis = alongY ? vec2(0.0, 1.0) : vec2(1.0, 0.0);
        float spacing = pc.smoothData.x / 3.0;
        vec3 sum = normal;
        for (int k = -3; k <= 3; k++) {
            if (k == 0) continue;
            float dist = abs(float(k)) * spacing;
            vec2 tapUV = uv + axis * (float(k) * spacing) * pc.reciprocalResolution.xy;
            float onScreen = step(0.0, tapUV.x) * step(tapUV.x, 1.0) * step(0.0, tapUV.y) * step(tapUV.y, 1.0);
            tapUV = clamp(tapUV, 0.0, 1.0);
            vec3 tapNormal = normalize(expandNormal(textureLod(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), tapUV, 0.0).xyz));
            float tapZ = textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), tapUV, 0.0).x * farZ;
            float tolerance = max(viewZ * 0.0005 * dist, 0.5);
            float plane = abs(dot(positionFromViewZ(tapUV, tapZ) - origin, normal));
            float weight = clamp(1.0 - plane / tolerance, 0.0, 1.0) * smoothstep(0.5, 0.8, dot(tapNormal, normal)) * onScreen;
            sum += tapNormal * weight;
        }
        normal = normalize(sum);
    }

    o_normal = vec4(compressNormal(normal), source.w);
}
