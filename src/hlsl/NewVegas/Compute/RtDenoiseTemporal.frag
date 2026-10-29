// UNOFFICIAL 2026-10-29 shared RT denoiser (core\RtDenoise.h; docs\next\FORK-RT-SUN.md "Denoiser"), pass 1 of 2: temporal
// accumulation (SVGF / ReBLUR-style). The noisy signal (.r) is blended with last frame's history reprojected through the depth and last
// frame's camera (camera-relative view-projection + the camera's move); disoccluded pixels (depth plane mismatch) restart.
// Output (RGBA16F history): r = value, g = second moment, b = history length (frames), a = linear view depth (for the next frame).
// Compile: glslangValidator -V --target-env vulkan1.1 -o RtDenoiseTemporal.spv RtDenoiseTemporal.frag
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_noisy;     // the RT pass' output (.r)
layout(set = 1, binding = 1) uniform texture2D t_history;   // last frame's output of this pass
layout(set = 1, binding = 2) uniform texture2D t_depth;     // TESR_DepthBuffer (.r = view z / farZ)

layout(push_constant) uniform Push {
    mat4 prevViewProj;   // last frame's camera-relative view-projection (D3D rows uploaded as columns: clip = M * p)
    vec4 forward;        // xyz camera forward, w farZ
    vec4 right;          // xyz right / P00
    vec4 up;             // xyz up / P11
    vec4 move;           // xyz this frame's camera position - last frame's (world units), w 1: reset (no history)
    vec4 params;         // x max history frames, y depth tolerance (relative), z 1 / width, w 1 / height
    uint samplerIndex;   // the point-clamp sampler (written by the binding)
    uint pad[3];
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    const float value = textureLod(sampler2D(t_noisy, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0).r;
    const float viewZ = textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0).r * pc.forward.w;
    vec4 result = vec4(value, value * value, 1.0, viewZ);
    if (pc.move.w < 0.5 && viewZ > 0.0) {
        // this pixel last frame: camera-relative now, + the camera's move = camera-relative then
        const vec3 p = (pc.forward.xyz + pc.right.xyz * (2.0 * uv.x - 1.0) - pc.up.xyz * (2.0 * uv.y - 1.0)) * viewZ + pc.move.xyz;
        const vec4 clip = pc.prevViewProj * vec4(p, 1.0);
        if (clip.w > 0.0) {
            const vec2 prevUv = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
            if (all(greaterThanEqual(prevUv, vec2(0.0))) && all(lessThanEqual(prevUv, vec2(1.0)))) {
                const vec4 history = textureLod(sampler2D(t_history, NVR_SAMPLER(pc.samplerIndex)), prevUv, 0.0);
                // the same surface: last frame's depth there matches this point's depth as seen from last frame
                if (abs(history.a - clip.w) <= pc.params.y * max(clip.w, 1.0) && history.b > 0.0) {
                    const float length = min(history.b + 1.0, pc.params.x);
                    const float alpha = 1.0 / length;
                    result.r = mix(history.r, value, alpha);
                    result.g = mix(history.g, value * value, alpha);
                    result.b = length;
                }
            }
        }
    }
    o_color = result;
}
