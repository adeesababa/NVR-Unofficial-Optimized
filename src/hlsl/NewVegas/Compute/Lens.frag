#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_source;
layout(set = 1, binding = 1) uniform texture2D t_bloom;
layout(set = 1, binding = 2) uniform texture2D t_dirt;

layout(push_constant) uniform Push {
    vec4 lensData;
    uint samplerIndex;
    uint wrapIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec4 color = linearize(texture(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), uv));
    vec4 dirtColor = vec4(pows(texture(sampler2D(t_dirt, NVR_SAMPLER(pc.wrapIndex)), uv).rgb, max(0.0, 3.0 - pc.lensData.z)), 1.0);
    vec4 bloom = texture(sampler2D(t_bloom, NVR_SAMPLER(pc.samplerIndex)), uv);
    float bloomLuma = luma(bloom);
    bloom = pows(bloomLuma, pc.lensData.y) * (bloom / max(bloomLuma, 1e-6));
    color += dirtColor.r * bloom * pc.lensData.x;
    o_color = delinearize(vec4(color.rgb, 1.0));
}
