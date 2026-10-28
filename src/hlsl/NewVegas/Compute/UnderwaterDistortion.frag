#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : enable
#include "Common.glsl"
#include "Underwater.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_source;
layout(set = 1, binding = 2) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    uint linearIndex;
    uint pad[3];
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    float depth = readDepth(uv, t_depth, pc.linearIndex);
    vec3 eyeVector = toWorld(uv);
    vec3 worldPos = cb.cameraPosition.xyz + eyeVector * depth;
    vec4 color = texture(sampler2D(t_source, NVR_SAMPLER(pc.linearIndex)), uv);
    if (aboveWater(cb.cameraPosition.z, worldPos.z, cb.waterSettings.x)) {
        o_color = color;
        return;
    }

    float frame = cb.gameTime.z * cb.waveParams.z * 1.5;
    vec2 uvD = uv;
    uvD.x += sin(frame * 3.0 + uv.x * 60.0) * 0.001;
    uvD.y += cos(frame * 3.0 + uv.y * 60.0) * 0.001;
    color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.linearIndex)), uvD);
    color.a = 1.0;
    o_color = color;
}
