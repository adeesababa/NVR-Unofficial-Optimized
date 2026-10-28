#version 450
#extension GL_GOOGLE_include_directive : enable
#include "BounceLight.glsl"

layout(set = 1, binding = 0) uniform texture2D t_bounce;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const int cKernelSize = 12;
const float BlurWeights[cKernelSize] = float[](
    0.057424882, 0.058107773, 0.061460144, 0.071020611, 0.088092873, 0.106530916,
    0.106530916, 0.088092873, 0.071020611, 0.061460144, 0.058107773, 0.057424882);
const float BlurSteps[cKernelSize] = float[](-6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0);

void main() {
    vec2 axis = pc.pass_.xy;
    vec4 center = POINT_LOD(t_bounce, uv);
    float tolerance = max(center.a * 0.03, 2.0);
    vec3 sum = center.rgb * 0.114725602;
    float weights = 0.114725602;
    for (int i = 0; i < cKernelSize; i++) {
        vec2 blurOffset = vec2(BlurSteps[i] * cb.reciprocalResolution.x, BlurSteps[i] * cb.reciprocalResolution.y);
        vec2 tapUV = uv + blurOffset * axis * BlurRadius * (cb.bounceLayout.zw / cb.reciprocalResolution.xy);
        vec4 tap = POINT_LOD(t_bounce, tapUV);
        float weight = BlurWeights[i] * (abs(tap.a - center.a) <= tolerance ? 1.0 : 0.0);
        sum += tap.rgb * weight;
        weights += weight;
    }
    o_color = vec4(sum / weights, center.a);
}
