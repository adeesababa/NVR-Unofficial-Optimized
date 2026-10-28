#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable
#include "AmbientLight.glsl"

layout(set = 1, binding = 0) uniform texture2D t_depth;
layout(set = 1, binding = 1) uniform texture2D t_buffer;

layout(location = 0) out vec4 o_color;

const float weights[4] = float[4](0.30, 0.23, 0.11, 0.05);

vec4 Blur(vec2 uvIn, vec2 axis) {
    float depth = readDepthLod(t_depth, uvIn);
    float tolerance = max(depth * 0.03, 2.0);
    float looseTolerance = max(depth * 0.25, 16.0);
    vec2 invTolerance = 1.0 / vec2(tolerance, looseTolerance);
    vec4 center = pointLod(t_buffer, uvIn);
    if (DebugView > 2.5 && DebugView < 6.5) return center;
    vec3 bounce = center.rgb * weights[0];
    float visibility = center.a * weights[0];
    vec2 total = vec2(weights[0]);
    [[unroll]] for (int i = 1; i < 4; i++) {
        [[unroll]] for (int side = -1; side <= 1; side += 2) {
            vec2 tapUV = uvIn + axis * cb.alLayout.zw * (float(i * side) * BlurStep);
            float dz = abs(readDepthLod(t_depth, tapUV) - depth);
            vec2 w = weights[i] * clamp(1.0 - dz * invTolerance, 0.0, 1.0);
            vec4 tap = pointLod(t_buffer, tapUV);
            visibility += tap.a * w.x;
            bounce += tap.rgb * w.y;
            total += w;
        }
    }
    return vec4(bounce / total.y, visibility / total.x);
}

void main() { o_color = Blur(frameUV(), (pc.variant & 1u) != 0u ? vec2(0.0, 1.0) : vec2(1.0, 0.0)); }
