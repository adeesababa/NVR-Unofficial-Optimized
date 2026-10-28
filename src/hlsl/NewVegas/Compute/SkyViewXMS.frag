#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

#define SAMPLE_TRANS(coord) textureLod(sampler2D(t_trans, NVR_SAMPLER(pc.samplerIndex)), (coord), 0.0)
#define SAMPLE_MS(coord) textureLod(sampler2D(t_ms, NVR_SAMPLER(pc.samplerIndex)), (coord), 0.0)

layout(set = 1, binding = 0) uniform texture2D t_trans;
layout(set = 1, binding = 1) uniform texture2D t_ms;

layout(push_constant) uniform Push {
    vec4 physSky;
    vec4 physSkySun;
    uint samplerIndex;
    vec2 uvShift;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#include "SkyAtmosphere.glsl"
#include "SkyTables.glsl"

void main() {
    vec2 tuv = uv + pc.uvShift;
    float azimuth = tuv.x * AtmPi;
    float elevation = tuv.y * tuv.y * (0.5 * AtmPi);
    vec3 dir = vec3(cos(elevation) * cos(azimuth), cos(elevation) * sin(azimuth), max(sin(elevation), 0.002));
    vec3 sun = vec3(sqrt(clamp(1.0 - pc.physSkySun.z * pc.physSkySun.z, 0.0, 1.0)), 0.0, pc.physSkySun.z);
    o_color = vec4(AtmSkyX(normalize(dir), sun, 300.0, pc.physSky.w, true) * TableScale, 1.0);
}
