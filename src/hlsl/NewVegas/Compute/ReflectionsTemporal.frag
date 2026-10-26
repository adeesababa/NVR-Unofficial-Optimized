// Reflections technique 3 (Temporal, half resolution) as a Vulkan pass: Effects/Reflections.fx.hlsl TemporalPS line for line.
#version 450
#extension GL_EXT_control_flow_attributes : enable
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = (vpos + 0.5) * LowTexel;
    vec4 current = textureLod(sampler2D(t_trace, POINT_S), uv, 0.0);
    float depth = readDepthLod(uv - 0.5 * (LowTexel - cb.reciprocalResolution.xy));

    vec4 low = current, high = current;
    [[unroll]] for (int y = -1; y <= 1; y++) {
        [[unroll]] for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            vec4 tap = textureLod(sampler2D(t_trace, POINT_S), uv + vec2(x, y) * LowTexel, 0.0);
            low = min(low, tap); high = max(high, tap);
        }
    }

    vec3 world = toWorld(uv) * depth;
    vec4 clip = cb.reproject * vec4(world, 1.0);
    float w = max(clip.w, 0.001);
    vec2 last = vec2(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5);
    float inside = float(all(greaterThan(last, vec2(0.0))) && all(lessThan(last, vec2(1.0))) && current.a + high.a > 0.0);
    vec4 history = textureLod(sampler2D(t_history, POINT_S), last, 0.0);
    history = clamp(history, low, high);
    o_color = mix(current, history, HistoryWeight * inside);
}
