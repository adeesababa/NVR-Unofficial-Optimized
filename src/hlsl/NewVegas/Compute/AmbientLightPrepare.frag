#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientLight.glsl"

layout(set = 1, binding = 0) uniform texture2D t_depth;
layout(set = 1, binding = 1) uniform texture2D t_normals;
layout(set = 1, binding = 2) uniform texture2D t_viewModel;

layout(location = 0) out vec4 o_color;

void main() {
    vec2 uv = frameUV();
    float viewZ = PositionFromLinear(uv, pointLod(t_depth, uv).x).z;
    if (IsViewModel(t_viewModel, uv)) viewZ = 1.0e6;
    vec3 normal = normalize(pointLod(t_normals, uv).xyz * 2.0 - 1.0);
    o_color = vec4(viewZ, Pack2(OctEncode(normal)), 0.0, 0.0);
}
