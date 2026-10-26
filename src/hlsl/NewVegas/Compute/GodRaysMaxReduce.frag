// GodRays SkipEmpty reduction (technique GodRaysMaxReduce, MaxReduce) as a Vulkan pass (needs NVR DXVK): Effects/GodRays.fx.hlsl
// line for line, one 8 x 8 step per draw (effects/GodRays.cpp RunReduce). VPOS is floor(gl_FragCoord.xy).
#version 450
#extension GL_EXT_control_flow_attributes : enable
#extension GL_GOOGLE_include_directive : enable
#include "GodRays.glsl"

layout(set = 1, binding = 0) uniform texture2D t_source;     // NVR_GodRaysBuffer (LINEAR, CLAMP): the step's source

layout(location = 0) out vec4 o_color;

void main() {
    vec2 origin = floor(gl_FragCoord.xy) * 8.0 + 0.5;
    float m = 0.0;
    [[unroll]] for (int y = 0; y < 8; y++) {
        [[unroll]] for (int x = 0; x < 8; x++) {
            vec3 c = LINEAR_LOD(t_source, (origin + vec2(x, y)) * pc.reduce.xy).rgb;
            m = max(m, max(c.r, max(c.g, c.b)));
        }
    }
    o_color = vec4(m, m, m, 1.0);
}
