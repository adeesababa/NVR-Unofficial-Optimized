#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_source;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_ortho;
layout(set = 1, binding = 3) uniform texture2D t_bloom;

layout(push_constant) uniform Push {
    mat4 shadowCameraToLight;
    mat4 viewTransform;
    vec4 viewScale;
    vec4 rainData;
    vec4 rainAspect;
    vec4 sunDirection;
    vec4 sunColor;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const int RainLayers = 15;
const float DEPTH = 100.0;
const float PI = 3.1415926538;
const mat3 p = mat3(30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122);

float fmodh(float x, float y) { return x - y * trunc(x / y); }
vec2 fmodh(vec2 x, float y) { return x - y * trunc(x / y); }

float smoothstepHlsl(float a, float b, float x) {
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord).x * pc.viewScale.z;
}

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(pc.viewTransform[0][2], pc.viewTransform[1][2], pc.viewTransform[2][2]);
    v += (1.0 / pc.viewScale.x * (2.0 * tex.x - 1.0)) * vec3(pc.viewTransform[0][0], pc.viewTransform[1][0], pc.viewTransform[2][0]);
    v += (-1.0 / pc.viewScale.y * (2.0 * tex.y - 1.0)) * vec3(pc.viewTransform[0][1], pc.viewTransform[1][1], pc.viewTransform[2][1]);
    return v;
}

vec2 cylindrical(vec3 world) {
    float u = -atan(world.y, world.x) / PI;
    float v = world.z / length(world.xy);
    return vec2(u * 0.5 + 0.5, 0.004 * pc.rainData.y * v);
}

float getOrtho(vec4 orthoPos) {
    orthoPos.xyz /= orthoPos.w;
    float outofbounds = (orthoPos.x < -1.0 || orthoPos.x > 1.0 ||
        orthoPos.y < -1.0 || orthoPos.y > 1.0 ||
        orthoPos.z < 0.0 || orthoPos.z > 1.0) ? 1.0 : 0.0;

    orthoPos.x = orthoPos.x * 0.5 + 0.5;
    orthoPos.y = orthoPos.y * -0.5 + 0.5;
    float ortho = textureLod(sampler2D(t_ortho, NVR_SAMPLER(pc.samplerIndex)), orthoPos.xy, 0.0).r;
    return float(ortho > orthoPos.z - 0.0001) * (1.0 - outofbounds);
}

void main() {
    vec4 color = linearize(texture(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), uv));

    vec3 rayStart = toWorld(uv);
    vec3 rayEnd = rayStart * DEPTH * float(RainLayers);
    vec4 orthoStart = pc.shadowCameraToLight * vec4(rayStart.xyz, 1.0);
    vec4 orthoEnd = pc.shadowCameraToLight * vec4(rayEnd.xyz, 1.0);
    vec4 orthoStep = (orthoEnd - orthoStart) / float(RainLayers);

    float depth = readDepth(uv);
    vec2 cyl = cylindrical(rayStart.xyz);
    float timetick = pc.viewScale.w;
    float speed = pc.rainData.z;

    float totalRain = 0.0;
    for (int i = 1; i < RainLayers; i++) {
        if (depth > DEPTH * float(i) || pc.rainAspect.w < 0.5) {
            vec2 noiseSurfacePosition = cyl * (1.0 + float(i) * DEPTH);
            noiseSurfacePosition += vec2(noiseSurfacePosition.y * (fmodh(float(i) * 107.238917, 1.0) - 0.5), speed * timetick);

            vec3 m = vec3(floor(noiseSurfacePosition), float(i));
            vec3 mp = m / max(fract(p * m), vec3(1e-4));
            vec3 rainNoise = fract(mp);

            vec2 shape = abs(fmodh(noiseSurfacePosition, 1.0) + rainNoise.xy - 1.0);
            shape += 0.01 * abs(2.0 * fract(10.0 * noiseSurfacePosition.yx) - 1.0);
            float dropGradient = 0.6 * max(shape.x - shape.y, shape.x + shape.y) + max(shape.x, shape.y) - 0.01;
            float edge = 0.005 + 0.05 * min(1.0 * float(i), 4.0 * pc.rainData.x);

            float drop = smoothstepHlsl(edge, -edge, dropGradient) * (rainNoise.x / (1.0 + 0.2 * float(i)));
            drop *= getOrtho(orthoStart + orthoStep * float(i)) * float(depth > DEPTH * float(i));
            totalRain += drop;
        }
    }

    vec4 sunColorLinear = linearize(pc.sunColor);
    vec4 rainColor = mix(vec4(0.5), sunColorLinear * 2.0, pow(saturate(dot(normalize(rayStart.xyz), pc.sunDirection.xyz)), 2.0));

    vec2 refractedUV = uv + vec2(totalRain * pc.rainAspect.x, -totalRain * pc.rainAspect.x);
    vec4 refractedColor = linearize(texture(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), refractedUV));
    refractedColor += texture(sampler2D(t_bloom, NVR_SAMPLER(pc.samplerIndex)), refractedUV);

    o_color = delinearize(mix(color, refractedColor + rainColor * pc.rainAspect.y * 0.02, totalRain * pc.rainData.w));
}
