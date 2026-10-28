#version 450
#extension GL_GOOGLE_include_directive : enable
#include "SpecularCommon.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_normals;
layout(set = 1, binding = 3) uniform texture2D t_pointShadow;
layout(set = 1, binding = 4) uniform texture2D t_depthViewModel;
layout(set = 1, binding = 5) uniform texture2D t_specularBuffer;

layout(push_constant) uniform Push {
    uint pointSampler;
    uint linearSampler;
    uint pad[2];
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec4 color = sampleLod(t_rendered, pc.pointSampler, uv);
    vec3 lin = linearize(color.rgb);
    if (!addSpecular(uv, readDepthLod(t_depth, pc.pointSampler, uv), pc.pointSampler, pc.linearSampler,
                     t_depthViewModel, t_normals, t_pointShadow, t_specularBuffer, lin)) {
        o_color = color;
        return;
    }
    o_color = vec4(delinearize(lin), color.a);
}
