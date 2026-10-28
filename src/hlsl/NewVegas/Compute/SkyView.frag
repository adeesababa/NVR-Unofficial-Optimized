#version 450
#extension GL_GOOGLE_include_directive : enable
#include "SkyAtmosphere.glsl"

layout(push_constant) uniform Push {
    vec4 physSky;
    vec4 physSkySun;
    uint samplerIndex;
    vec2 uvShift;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec2 tuv = uv + pc.uvShift;
    float azimuth = tuv.x * AtmPi;
    float elevation = tuv.y * tuv.y * (0.5 * AtmPi);
    vec3 dir = vec3(cos(elevation) * cos(azimuth), cos(elevation) * sin(azimuth), max(sin(elevation), 0.002));
    vec3 sun = vec3(sqrt(clamp(1.0 - pc.physSkySun.z * pc.physSkySun.z, 0.0, 1.0)), 0.0, pc.physSkySun.z);
    o_color = vec4(AtmSky(normalize(dir), sun, 300.0, pc.physSky.w) * TableScale, 1.0);
}
