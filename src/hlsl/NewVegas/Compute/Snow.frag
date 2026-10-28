#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_ortho;

layout(push_constant) uniform Push {
    mat4 shadowToLight;
    vec4 viewColumn0;
    vec4 viewColumn1;
    vec4 viewColumn2;
    vec4 gameTime;
    vec4 snowData;
    vec4 sunColor;
    vec4 sunAmbient;
    vec4 projectionFar;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const float PI = 3.14159265;
const float hscale = 0.1;
const mat3 p = mat3(30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122, 30.323122);
const float DEPTH = 50.0;
const float angle = 1.0;
const int SnowLayers = 8;

float fmodHlsl(float x, float y) { return x - y * trunc(x / y); }
vec2 fmodHlsl(vec2 x, float y) { return x - y * trunc(x / y); }

vec3 toWorld(vec2 tex) {
    vec3 v = pc.viewColumn2.xyz;
    v += (1.0 / pc.projectionFar.x * (2.0 * tex.x - 1.0)) * pc.viewColumn0.xyz;
    v += (-1.0 / pc.projectionFar.y * (2.0 * tex.y - 1.0)) * pc.viewColumn1.xyz;
    return v;
}

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord).x * pc.projectionFar.z;
}

vec2 cylindrical(vec3 world) {
    float u = -atan(world.y, world.x) / PI;
    float v = world.z / length(world.xy);
    return vec2(0.5 * u + 0.5, hscale * v);
}

float getOrtho(vec4 orthoPos) {
    orthoPos.xyz /= orthoPos.w;
    float outofbounds = float(orthoPos.x < -1.0 || orthoPos.x > 1.0 ||
        orthoPos.y < -1.0 || orthoPos.y > 1.0 ||
        orthoPos.z < 0.0 || orthoPos.z > 1.0);

    orthoPos.x = orthoPos.x * 0.5 + 0.5;
    orthoPos.y = orthoPos.y * -0.5 + 0.5;
    float ortho = texture(sampler2D(t_ortho, NVR_SAMPLER(pc.samplerIndex)), orthoPos.xy).r;
    return float((ortho > orthoPos.z - 0.0001) || outofbounds != 0.0);
}

void main() {
    vec4 color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv);
    color.rgb = pows(color.rgb, 2.2);

    vec3 rayStart = toWorld(uv);
    vec3 rayEnd = rayStart * DEPTH * float(SnowLayers);
    vec4 orthoStart = pc.shadowToLight * vec4(rayStart, 1.0);
    vec4 orthoEnd = pc.shadowToLight * vec4(rayEnd, 1.0);
    vec4 rayStep = (orthoEnd - orthoStart) / float(SnowLayers);

    float depth = readDepth(uv);
    vec2 cyl = cylindrical(rayStart);
    const float timetick = pc.gameTime.z;
    const float speed = pc.snowData.z;

    float totalSnow = 0.0;

    for (int i = 1; i < SnowLayers; i++) {
        float fi = float(i);
        vec2 noiseSurfacePosition = cyl * (1.0 + fi * DEPTH);
        float wind = sin(timetick * 0.5 + fi) * angle;
        noiseSurfacePosition += vec2(wind * noiseSurfacePosition.y * (fmodHlsl(fi * 107.238917, 1.0) - 0.5), speed * timetick);

        vec3 m = vec3(floor(noiseSurfacePosition), fi);
        vec3 mp = m / fract(p * m);
        vec3 snowNoise = fract(mp);

        vec2 shape = abs(fmodHlsl(noiseSurfacePosition, 1.0) + snowNoise.xy - 1.0);
        shape += 0.01 * abs(2.0 * fract(10.0 * noiseSurfacePosition.yx) - 1.0);
        float dropGradient = 0.6 * max(shape.x - shape.y, shape.x + shape.y) + max(shape.x, shape.y) - 0.01;
        float edge = 0.005 + 0.05 * min(1.0 * fi, 4.0 * pc.snowData.x);

        float t = clamp((dropGradient - edge) / (-edge - edge), 0.0, 1.0);
        float flake = t * t * (3.0 - 2.0 * t) * snowNoise.x;
        flake *= getOrtho(orthoStart + rayStep * fi) * float(depth > DEPTH * fi);
        totalSnow += flake;
    }
    totalSnow = clamp(totalSnow, 0.0, 1.0);

    vec4 snowColor = max(vec4(pows(pc.sunAmbient.rgb, 2.2), pc.sunAmbient.a), vec4(pows(pc.sunColor.rgb, 2.2), pc.sunColor.a));

    color = mix(color, snowColor, totalSnow);
    color.rgb = pows(color.rgb, 1.0 / 2.2);

    o_color = color;
}
