#version 450

layout(set = 1, binding = 0, std430) readonly buffer Accum {
    uint accum[];
};

layout(push_constant) uniform Push {
    uint width;
    uint height;
    uint pad0, pad1;
} pc;

layout(location = 0) out vec4 o_color;

void main() {
    uvec2 p = min(uvec2(gl_FragCoord.xy), uvec2(pc.width - 1u, pc.height - 1u));
    uint base = (p.y * pc.width + p.x) * 3u;
    o_color = vec4(vec3(accum[base], accum[base + 1u], accum[base + 2u]) * (1.0 / 65536.0), 1.0);
}
